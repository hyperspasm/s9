#include "s9_app.h"
#include "s9_components.h"
#include "s9_pn.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3/SDL_audio.h>
#include <math.h>

static SDL_Rect compute_viewport(const s9_layout_t *layout, const SDL_Rect *parent) {
	SDL_Rect vp;
	vp.x = parent->x + layout->margin_left;
	vp.y = parent->y + layout->margin_top;
	vp.w = layout->fixed_width  ? layout->fixed_width  : parent->w - layout->margin_left - layout->margin_right;
	vp.h = layout->fixed_height ? layout->fixed_height : parent->h - layout->margin_top - layout->margin_bottom;
	return vp;
}

#define S9_MAX_COMPONENTS 256
#define S9_AUDIO_STALL_MS 500    // audio not arriving for this long has the input reopened
#define S9_AUDIO_RETRY_MS 1000   // between attempts to open an input while there is none

typedef struct s9_app {
	s9_app_config_t config;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_RendererInfo renderer_info;
	SDL_DisplayMode display_mode;

	s9_components_t *components;
	s9_component_slot_t slots[S9_MAX_COMPONENTS]; // NULL-terminated (component == NULL)
	const s9_mode_slot_t *current_mode; // NULL until the first mode is applied
	s9_mode_id_t mode;
	bool show_fps;
	s9_pn_t *pn;

	SDL_AudioDeviceID audio_device_id;     // 0 while there is no input open
	SDL_AudioSpec audio_spec;
	s9_audio_stream_config_t stream_config;
	SDL_AtomicInt audio_ticks;              // when the callback last ran, in SDL's ticks, for the watchdog
	uint32_t audio_retry_ticks;            // when to try opening an input again, while there is none
	bool audio_waiting;                    // there is no input, and the wait has been logged

	int ret;
} s9_app_t;

static s9_app_t *g_app = NULL; // singleton instance

// -- private

static void print_renderer_info(void) {
	S9_LOG_INFO("Renderer: %s", g_app->renderer_info.name);
	if (g_app->renderer_info.flags & SDL_RENDERER_SOFTWARE) S9_LOG_INFO("    * Software");
	if (g_app->renderer_info.flags & SDL_RENDERER_ACCELERATED) S9_LOG_INFO("    * Accelerated");
	if (g_app->renderer_info.flags & SDL_RENDERER_PRESENTVSYNC) S9_LOG_INFO("    * VSync");
	if (g_app->renderer_info.flags & SDL_RENDERER_TARGETTEXTURE) S9_LOG_INFO("    * Render to texture");
	S9_LOG_INFO("    Max texture %d x %d", g_app->renderer_info.max_texture_width, g_app->renderer_info.max_texture_height);
	
	S9_LOG_INFO("Display mode:");
	S9_LOG_INFO("    %s", SDL_GetPixelFormatName(g_app->display_mode.format));
	S9_LOG_INFO("    %d x %d", g_app->display_mode.w, g_app->display_mode.h);
	S9_LOG_INFO("    %d Hz", g_app->display_mode.refresh_rate);
}

static void render(SDL_Renderer *renderer) {
	SDL_Rect default_viewport;
	SDL_GetRenderViewport(renderer, &default_viewport);
	const s9_component_t *fps = s9_components_get(g_app->components, COMP_FPS);

	for (s9_component_slot_t *s = g_app->slots; s->component; s++) {
		if (s->component == fps && !g_app->show_fps) continue;
		if (s->component->render) {
			SDL_Rect viewport = compute_viewport(&s->layout, &default_viewport);
			SDL_SetRenderViewport(renderer, &viewport);
			s->component->render(s->component->instance, renderer);
		}
	}

	SDL_SetRenderViewport(renderer, &default_viewport);
}

static void apply_mode(const s9_mode_slot_t *mode) {
	s9_component_slot_t *slot = g_app->slots;
	for (const s9_mode_slot_t *m = mode; m->id != COMP_NONE; m++) {
		*slot++ = (s9_component_slot_t){
			.component = s9_components_get(g_app->components, m->id),
			.layout = m->layout,
		};
	}
	slot->component = NULL; // sentinel
}

// The mode's components into the slot table the audio callback feeds. The
// callback runs under the device's lock, so the lock is held for the swap
// alone: a component starts streaming before the audio thread can reach it
// and stops after it no longer can. Holding the lock through their setup
// would keep the audio thread from the device for longer than its buffer
// holds, and overrun it.
static void switch_mode(s9_mode_id_t mode) {
	const s9_mode_slot_t *new_mode = s9_mode_slots(mode);

	int incoming[COMP_COUNT] = {0};
	for (const s9_mode_slot_t *m = new_mode; m->id != COMP_NONE; m++)
		incoming[m->id] = 1;

	int outgoing[COMP_COUNT] = {0};
	if (g_app->current_mode) {
		for (const s9_mode_slot_t *m = g_app->current_mode; m->id != COMP_NONE; m++)
			outgoing[m->id] = 1;
	}

	// prepare components entering the mode
	for (int i = 0; i < COMP_COUNT; i++) {
		if (incoming[i] && !outgoing[i]) {
			s9_component_t *c = s9_components_get(g_app->components, i);
			if (c && c->prepare_to_stream)
				c->prepare_to_stream(c->instance, &g_app->stream_config);
		}
	}

	// rebuild slot array
	SDL_LockAudioDevice(g_app->audio_device_id);
	apply_mode(new_mode);
	g_app->current_mode = new_mode;
	g_app->mode = mode;
	SDL_UnlockAudioDevice(g_app->audio_device_id);

	// stop streaming on components that left the mode
	for (int i = 0; i < COMP_COUNT; i++) {
		if (outgoing[i] && !incoming[i]) {
			s9_component_t *c = s9_components_get(g_app->components, i);
			if (c && c->stop_streaming) c->stop_streaming(c->instance);
		}
	}
}

// a mode change made here, rather than requested by a client, is pushed to the client
static void key_set_mode(s9_mode_id_t mode) {
	if (s9_app_set_mode(mode) == 0)
		s9_pn_notify_mode(g_app->pn);
}

// the audio drivers this build of SDL offers, the names the audio_driver setting takes
static void log_audio_drivers(void) {
	char names[256] = "";
	for (int i = 0; i < SDL_GetNumAudioDrivers(); i++) {
		size_t used = strlen(names);
		snprintf(names + used, sizeof(names) - used, "%s%s", i ? " " : "", SDL_GetAudioDriver(i));
	}
	S9_LOG_INFO("Available audio drivers: %s", names);
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
	(void)userdata;
	SDL_SetAtomicInt(&g_app->audio_ticks, (int)(uint32_t)SDL_GetTicks());

	for (s9_component_slot_t *s = g_app->slots; s->component; s++) {
		if (s->component->enqueue_audio) {
			s->component->enqueue_audio(s->component->instance, stream, len);
		}
	}
}

// The input: the device the configuration names when it is there, else the
// first listed, opened for float stereo at the configured rate and left
// paused; then the stream config the components are prepared with, from the
// device's format and the display's rate. Returns 1 when there is no device
// at all and -1 when one would not open, quietly when asked, for the
// retries while there is none.
static int open_audio_input(bool quiet) {
	int num_audio_devices = SDL_GetNumAudioDevices(SDL_TRUE);
	if (num_audio_devices <= 0) {
		if (!quiet) S9_LOG_ERROR("No available audio input devices");
		return 1;
	}
	
	// choose first device as default
	const char *selected_audio_device_name = SDL_GetAudioDeviceName(0, SDL_TRUE);
	
	// enumerate audio devices and select the specified one, if any
	if (!quiet) S9_LOG_INFO("Available audio input devices:");
	for (int i = 0; i < num_audio_devices; i++) {
		const char *device_name = SDL_GetAudioDeviceName(i, SDL_TRUE);
		if (!quiet) S9_LOG_INFO("    %s", device_name);
		if (g_app->config.audio_device != NULL && device_name && strcmp(device_name, g_app->config.audio_device) == 0) {
			selected_audio_device_name = device_name;
		}
	}	
	
	// open audio device with requested settings
	SDL_AudioSpec desired_spec;
	SDL_zero(desired_spec);
	desired_spec.freq = g_app->config.sample_rate;
	desired_spec.format = AUDIO_F32;
	desired_spec.channels = 2;
	desired_spec.samples = 256;
	desired_spec.callback = audio_callback;
	
	g_app->audio_device_id = SDL_OpenAudioDevice(selected_audio_device_name, SDL_TRUE, &desired_spec, &g_app->audio_spec, 0);
	if (g_app->audio_device_id == 0) {
		if (!quiet) S9_LOG_ERROR("Failed to open audio device: %s", SDL_GetError());
		return -1;
	}
	
	// print audio device info
	S9_LOG_INFO("Selected audio input device: %s", selected_audio_device_name);
	S9_LOG_INFO("    Sample Rate: %d Hz", g_app->audio_spec.freq);
	S9_LOG_INFO("    Channels: %d", g_app->audio_spec.channels);
	S9_LOG_INFO("    Buffer Size: %d samples", g_app->audio_spec.samples);
	
	// init stream_config; a display that does not say its refresh rate is taken as 60 Hz
	int refresh_rate = g_app->display_mode.refresh_rate > 0 ? g_app->display_mode.refresh_rate : 60;
	g_app->stream_config = (s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32,
		.sample_rate = g_app->audio_spec.freq,
		.channels = g_app->audio_spec.channels,
		.buffer_frames = g_app->audio_spec.samples,
		.samples_per_display_frame = ceil(g_app->audio_spec.freq / (double)refresh_rate),
	};
	return 0;
}

// streaming from the input, the watchdog counting from now
static void start_audio_input(void) {
	SDL_SetAtomicInt(&g_app->audio_ticks, (int)(uint32_t)SDL_GetTicks());
	SDL_PauseAudioDevice(g_app->audio_device_id, SDL_FALSE);
}

// The input opened again after it stalled or went away, or tried again
// while there is none: the components of the mode are prepared afresh when
// the new device's format differs from the one they stream, and streaming
// resumes. Without a device to open, the attempt is repeated later.
static void reopen_audio_input(const char *why) {
	if (g_app->audio_device_id) {
		S9_LOG_INFO("Audio input %s: reopening", why);
		SDL_CloseAudioDevice(g_app->audio_device_id);
		g_app->audio_device_id = 0;
	}
	s9_audio_stream_config_t was = g_app->stream_config;
	if (open_audio_input(g_app->audio_waiting) != 0) {
		if (!g_app->audio_waiting) S9_LOG_INFO("No audio input: waiting for one");
		g_app->audio_waiting = true;
		g_app->audio_retry_ticks = (uint32_t)SDL_GetTicks() + S9_AUDIO_RETRY_MS;
		return;
	}
	g_app->audio_waiting = false;
	if (!S9_AUDIO_STREAM_CONFIG_EQUALS(was, g_app->stream_config)) {
		for (s9_component_slot_t *s = g_app->slots; s->component; s++) {
			if (s->component->stop_streaming) s->component->stop_streaming(s->component->instance);
			if (s->component->prepare_to_stream) s->component->prepare_to_stream(s->component->instance, &g_app->stream_config);
		}
	}
	start_audio_input();
}

// --

int s9_app_create(const s9_app_config_t *config) {
	int ret;
	CHECK_PARAMETER(config != NULL, return -1);
	CHECK_ERROR(g_app == NULL, return -1, "ignoring attempt to create a second app instance");
	
	// set these up ASAP
	if(config->log_line) s9_set_logging_function(config->log_line);
	if(config->console_print) s9_set_console_print_function(config->console_print);
	s9_set_log_level(config->log_level);
	
	g_app = malloc(sizeof(s9_app_t));
	CHECK_ALLOC(g_app, return -1);
	memset(g_app, 0, sizeof(*g_app));
	
	g_app->config = *config;
	
	// the audio driver, when the configuration names one
	if (g_app->config.audio_driver) SDL_SetHint(SDL_HINT_AUDIODRIVER, g_app->config.audio_driver);

	// initialize SDL
	ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO); 
	CHECK_ERROR(ret == 0,
				log_audio_drivers();
				free(g_app);
				g_app = NULL;
				return -1,
				"SDL_Init failed: %s", SDL_GetError());
	S9_LOG_INFO("Audio driver: %s", SDL_GetCurrentAudioDriver());
	log_audio_drivers();
	
	// initialize SDL_ttf
	ret = TTF_Init();
	CHECK_ERROR(ret == 0,
				free(g_app);
				g_app = NULL;
				SDL_Quit();
				return -1,
				"TTF_Init failed: %d", ret);
	
	// NOTE: overriding SDL_WINDOW_ALLOW_HIGHDPI below until I can figure out how to actually make this work properly
	SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");
	
	// create window
	g_app->window = SDL_CreateWindow("S9",
									 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
									 g_app->config.window_width, g_app->config.window_height,
									 SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
	CHECK_ERROR(g_app->window != NULL,
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"SDL_CreateWindow failed: %s", SDL_GetError());
	
	// create renderer
	g_app->renderer = SDL_CreateRenderer(g_app->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	CHECK_ERROR(g_app->renderer != NULL,
				SDL_DestroyWindow(g_app->window);
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"SDL_CreateRenderer failed: %s", SDL_GetError());
	
	// s9 has no pointer interaction; this also keeps the pointer off a display that has no mouse
	SDL_ShowCursor(SDL_DISABLE);

	// get renderer info
	CHECK_ERROR(SDL_GetRendererInfo(g_app->renderer, &g_app->renderer_info) == 0,
				SDL_DestroyWindow(g_app->window);
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"SDL_GetRendererInfo failed: %s", SDL_GetError());
		
	// get display index
	int display_index = SDL_GetWindowDisplayIndex(g_app->window);
	CHECK_ERROR(display_index >= 0,
				SDL_DestroyWindow(g_app->window);
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"SDL_GetWindowDisplayIndex failed: %s", SDL_GetError());
	
	// get display mode
	CHECK_ERROR(SDL_GetCurrentDisplayMode(display_index, &g_app->display_mode) == 0,
				SDL_DestroyWindow(g_app->window);
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"SDL_GetCurrentDisplayMode failed: %s", SDL_GetError());
	
	// print info
	print_renderer_info();
	
	// enable blending mode for anti-aliasing
	SDL_SetRenderDrawBlendMode(g_app->renderer, SDL_BLENDMODE_BLEND);
	
	// create components
	g_app->components = s9_components_new();
	CHECK_ALLOC(g_app->components, s9_app_destroy(); return -1);

	// create pn
	g_app->pn = s9_pn_new(&(s9_pn_config_t){
		.components = g_app->components,
		.port = g_app->config.port,
		.www_dir = g_app->config.www_dir,
	});
	CHECK_ALLOC(g_app->pn, s9_app_destroy(); return -1);

	// the audio input
	if (open_audio_input(false) != 0) {
		s9_app_destroy();
		return -1;
	}

	// apply initial mode (prepares components for streaming)
	switch_mode(S9_MODE_FULL);

	// start streaming audio
	start_audio_input();
	return 0;
}

void s9_app_destroy(void) {
	CHECK_ERROR(g_app != NULL, return, "ignoring attempt to destroy uncreated app");

	// close the audio device before the components its callback feeds go
	if (g_app->audio_device_id > 0) {
		SDL_PauseAudioDevice(g_app->audio_device_id, SDL_TRUE);
		SDL_CloseAudioDevice(g_app->audio_device_id);
	}

	// destroy components
	s9_components_free(&g_app->components);

	// close pn
	s9_pn_free(&g_app->pn);

	TTF_Quit();
	SDL_Quit();

	free(g_app);
	g_app = NULL;
}

int s9_app_run(void) {
	SDL_Event e;
	
	CHECK_ERROR(g_app != NULL, return -1, "app not created");
	
	// exit if necessary
	if (g_app->ret) return g_app->ret;

	// poll until all events are handled!
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT) {
			g_app->ret = 1;
		} else if (e.type == SDL_KEYDOWN) {
			S9_LOG_TRACE("key pressed: %s", SDL_GetKeyName(e.key.keysym.sym));
			switch (e.key.keysym.sym) {
			case SDLK_1: key_set_mode(S9_MODE_FULL);     break;
			case SDLK_2: key_set_mode(S9_MODE_SCOPE);    break;
			case SDLK_3: key_set_mode(S9_MODE_SPECTRUM); break;
			case SDLK_4: key_set_mode(S9_MODE_PERIODIC); break;
			case SDLK_5: key_set_mode(S9_MODE_SPECTROGRAM); break;
			default: break;
			}
		} else if (e.type == SDL_AUDIODEVICEREMOVED && e.adevice.iscapture && e.adevice.which == g_app->audio_device_id) {
			reopen_audio_input("device removed");
		} else if (e.type == SDL_AUDIODEVICEADDED && e.adevice.iscapture && g_app->audio_device_id == 0) {
			reopen_audio_input(NULL);
		}
	}
	
	// exit if necessary
	if (g_app->ret) return g_app->ret;

	// the input watched: reopened when its audio stops arriving, tried again while there is none
	uint32_t now = (uint32_t)SDL_GetTicks();
	if (g_app->audio_device_id) {
		if (now - (uint32_t)SDL_GetAtomicInt(&g_app->audio_ticks) > S9_AUDIO_STALL_MS) reopen_audio_input("stalled");
	} else if ((int32_t)(now - g_app->audio_retry_ticks) >= 0) {
		reopen_audio_input(NULL);
	}
	
	// poll pn
	s9_pn_update(g_app->pn);

	// reset viewport to default
	SDL_SetRenderViewport(g_app->renderer, NULL);
	
	// clear screen
	SDL_SetRenderDrawColor(g_app->renderer, 0, 0, 0, 255);
	SDL_RenderClear(g_app->renderer);
	
	// perform all rendering
	render(g_app->renderer);
	
	// present renderer
	SDL_RenderPresent(g_app->renderer);

	return 0;
}

int s9_app_set_mode(s9_mode_id_t mode) {
	CHECK_ERROR(g_app != NULL, return -1, "app not created");
	CHECK_PARAMETER(s9_mode_slots(mode) != NULL, return -1);
	if (g_app->current_mode && mode == g_app->mode) return 0;
	switch_mode(mode);
	return 0;
}

s9_mode_id_t s9_app_get_mode(void) {
	return g_app ? g_app->mode : S9_MODE_FULL;
}

void s9_app_set_show_fps(bool show) {
	if (g_app) g_app->show_fps = show;
}

bool s9_app_get_show_fps(void) {
	return g_app ? g_app->show_fps : false;
}
