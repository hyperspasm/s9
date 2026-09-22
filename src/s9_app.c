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
#define S9_AUDIO_BUFFER_FRAMES "256"   // the buffer asked of the input device, in frames, as the hint takes it
#define S9_AUDIO_CHUNK_BYTES 8192      // read from the stream at a time

typedef struct s9_app {
	s9_app_config_t config;
	SDL_Window *window;
	SDL_Renderer *renderer;
	const SDL_DisplayMode *display_mode;   // SDL's, for the window's display

	s9_components_t *components;
	s9_component_slot_t slots[S9_MAX_COMPONENTS]; // NULL-terminated (component == NULL)
	const s9_mode_slot_t *current_mode; // NULL until the first mode is applied
	s9_mode_id_t mode;
	bool show_fps;
	s9_pn_t *pn;

	SDL_AudioStream *audio_stream;         // the input, open through its stream; NULL while there is none
	SDL_AudioDeviceID audio_device_id;     // the device the stream opened, 0 while there is none
	SDL_AudioSpec audio_spec;              // what the stream delivers, whatever the device runs at
	s9_audio_stream_config_t stream_config;
	SDL_AtomicInt audio_ticks;              // when the callback last ran, in SDL's ticks, for the watchdog
	uint32_t audio_retry_ticks;            // when to try opening an input again, while there is none
	bool audio_waiting;                    // there is no input, and the wait has been logged

	int ret;
} s9_app_t;

static s9_app_t *g_app = NULL; // singleton instance

// -- private

static void print_renderer_info(void) {
	SDL_PropertiesID props = SDL_GetRendererProperties(g_app->renderer);
	S9_LOG_INFO("Renderer: %s", SDL_GetRendererName(g_app->renderer));
	if (SDL_GetNumberProperty(props, SDL_PROP_RENDERER_VSYNC_NUMBER, 0) != 0) S9_LOG_INFO("    * VSync");
	S9_LOG_INFO("    Max texture %d", (int)SDL_GetNumberProperty(props, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0));
	
	S9_LOG_INFO("Display mode:");
	S9_LOG_INFO("    %s", SDL_GetPixelFormatName(g_app->display_mode->format));
	S9_LOG_INFO("    %d x %d", g_app->display_mode->w, g_app->display_mode->h);
	S9_LOG_INFO("    %g Hz", g_app->display_mode->refresh_rate);
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
// callback runs under the stream's lock, so the lock is held for the swap
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
	if (g_app->audio_stream) SDL_LockAudioStream(g_app->audio_stream);
	apply_mode(new_mode);
	g_app->current_mode = new_mode;
	g_app->mode = mode;
	if (g_app->audio_stream) SDL_UnlockAudioStream(g_app->audio_stream);

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

// The device's audio as it arrives, in the stream's format, to the components
// of the mode. Runs on SDL's audio thread with the stream locked; switch_mode
// takes the same lock to change the components.
static void audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
	(void)userdata;
	(void)additional_amount;
	(void)total_amount;
	SDL_SetAtomicInt(&g_app->audio_ticks, (int)(uint32_t)SDL_GetTicks());
	uint8_t chunk[S9_AUDIO_CHUNK_BYTES];
	for (;;) {
		int len = SDL_GetAudioStreamData(stream, chunk, (int)sizeof(chunk));
		if (len <= 0) break;
		for (s9_component_slot_t *s = g_app->slots; s->component; s++) {
			if (s->component->enqueue_audio) {
				s->component->enqueue_audio(s->component->instance, chunk, len);
			}
		}
	}
}

// recording devices in name order, so the choice among several is the same from run to run
static int compare_device_names(const void *a, const void *b) {
	const char *na = SDL_GetAudioDeviceName(*(const SDL_AudioDeviceID *)a);
	const char *nb = SDL_GetAudioDeviceName(*(const SDL_AudioDeviceID *)b);
	return strcmp(na ? na : "", nb ? nb : "");
}

// The input: the device the configuration names when it is there, else the
// first in name order that opens, through a stream that delivers float
// stereo at the configured rate whatever the device runs at, left paused;
// then the stream config the components are prepared with, from that
// format and the display's rate. Returns 1 when there is no device at all
// and -1 when none would open, quietly when asked, for the retries while
// there is none.
static int open_audio_input(bool quiet) {
	int num_audio_devices = 0;
	SDL_AudioDeviceID *audio_devices = SDL_GetAudioRecordingDevices(&num_audio_devices);
	if (audio_devices == NULL || num_audio_devices <= 0) {
		SDL_free(audio_devices);
		if (!quiet) S9_LOG_ERROR("No available audio input devices");
		return 1;
	}
	qsort(audio_devices, num_audio_devices, sizeof(*audio_devices), compare_device_names);
	if (!quiet) S9_LOG_INFO("Available audio input devices:");
	for (int i = 0; i < num_audio_devices; i++) {
		if (!quiet) S9_LOG_INFO("    %s", SDL_GetAudioDeviceName(audio_devices[i]));
	}
	
	// open the named device, else the first that opens
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, S9_AUDIO_BUFFER_FRAMES);
	SDL_AudioSpec desired_spec = { SDL_AUDIO_F32, 2, g_app->config.sample_rate };
	const char *selected_audio_device_name = NULL;
	for (int pass = 0; pass < 2 && g_app->audio_stream == NULL; pass++) {
		for (int i = 0; i < num_audio_devices && g_app->audio_stream == NULL; i++) {
			const char *device_name = SDL_GetAudioDeviceName(audio_devices[i]);
			bool named = device_name && g_app->config.audio_device && strcmp(device_name, g_app->config.audio_device) == 0;
			if (pass == 0 ? !named : named) continue;
			g_app->audio_stream = SDL_OpenAudioDeviceStream(audio_devices[i], &desired_spec, audio_callback, NULL);
			if (g_app->audio_stream) selected_audio_device_name = device_name;
			else if (!quiet) S9_LOG_INFO("    %s did not open: %s", device_name, SDL_GetError());
		}
	}
	SDL_free(audio_devices);
	if (g_app->audio_stream == NULL) {
		if (!quiet) S9_LOG_ERROR("Failed to open an audio input device");
		return -1;
	}
	g_app->audio_device_id = SDL_GetAudioStreamDevice(g_app->audio_stream);
	
	// print audio device info: the device's format and, where it differs, the stream's
	SDL_AudioSpec device_spec;
	int device_frames = 0;
	SDL_GetAudioDeviceFormat(g_app->audio_device_id, &device_spec, &device_frames);
	SDL_GetAudioStreamFormat(g_app->audio_stream, NULL, &g_app->audio_spec);
	S9_LOG_INFO("Selected audio input device: %s", selected_audio_device_name);
	S9_LOG_INFO("    Sample Rate: %d Hz", device_spec.freq);
	S9_LOG_INFO("    Channels: %d", device_spec.channels);
	S9_LOG_INFO("    Buffer Size: %d samples", device_frames);
	if (device_spec.freq != g_app->audio_spec.freq || device_spec.channels != g_app->audio_spec.channels) {
		S9_LOG_INFO("    Converted to %d Hz, %d channels", g_app->audio_spec.freq, g_app->audio_spec.channels);
	}
	
	// init stream_config; a display that does not say its refresh rate is taken as 60 Hz
	float refresh_rate = g_app->display_mode->refresh_rate > 0.0f ? g_app->display_mode->refresh_rate : 60.0f;
	g_app->stream_config = (s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32,
		.sample_rate = g_app->audio_spec.freq,
		.channels = g_app->audio_spec.channels,
		.buffer_frames = device_frames,
		.samples_per_display_frame = ceil(g_app->audio_spec.freq / refresh_rate),
	};
	return 0;
}

// streaming from the input, the watchdog counting from now
static void start_audio_input(void) {
	SDL_SetAtomicInt(&g_app->audio_ticks, (int)(uint32_t)SDL_GetTicks());
	SDL_ResumeAudioStreamDevice(g_app->audio_stream);
}

// The input opened again after it stalled or went away, or tried again
// while there is none: the components of the mode are prepared afresh when
// the new device's format differs from the one they stream, and streaming
// resumes. Without a device to open, the attempt is repeated later.
static void reopen_audio_input(const char *why) {
	if (g_app->audio_stream) {
		S9_LOG_INFO("Audio input %s: reopening", why);
		SDL_DestroyAudioStream(g_app->audio_stream);   // closes the device with it
		g_app->audio_stream = NULL;
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
	if (g_app->config.audio_driver) SDL_SetHint(SDL_HINT_AUDIO_DRIVER, g_app->config.audio_driver);

	// initialize SDL
	CHECK_ERROR(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO),
				log_audio_drivers();
				free(g_app);
				g_app = NULL;
				return -1,
				"SDL_Init failed: %s", SDL_GetError());
	S9_LOG_INFO("Audio driver: %s", SDL_GetCurrentAudioDriver());
	log_audio_drivers();
	
	// initialize SDL_ttf
	CHECK_ERROR(TTF_Init(),
				free(g_app);
				g_app = NULL;
				SDL_Quit();
				return -1,
				"TTF_Init failed: %s", SDL_GetError());
	
	// create window
	g_app->window = SDL_CreateWindow("S9", g_app->config.window_width, g_app->config.window_height, 0);
	CHECK_ERROR(g_app->window != NULL,
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"SDL_CreateWindow failed: %s", SDL_GetError());
	
	// create renderer, presenting in step with the display
	g_app->renderer = SDL_CreateRenderer(g_app->window, NULL);
	CHECK_ERROR(g_app->renderer != NULL,
				SDL_DestroyWindow(g_app->window);
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"SDL_CreateRenderer failed: %s", SDL_GetError());
	SDL_SetRenderVSync(g_app->renderer, 1);
	
	// s9 has no pointer interaction; this also keeps the pointer off a display that has no mouse
	SDL_HideCursor();

	// the mode of the window's display
	SDL_DisplayID display = SDL_GetDisplayForWindow(g_app->window);
	CHECK_ERROR(display != 0,
				SDL_DestroyRenderer(g_app->renderer);
				SDL_DestroyWindow(g_app->window);
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"SDL_GetDisplayForWindow failed: %s", SDL_GetError());
	g_app->display_mode = SDL_GetCurrentDisplayMode(display);
	CHECK_ERROR(g_app->display_mode != NULL,
				SDL_DestroyRenderer(g_app->renderer);
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

	// close the audio device, and with it the stream feeding the components, before they go
	if (g_app->audio_stream) {
		SDL_DestroyAudioStream(g_app->audio_stream);
		g_app->audio_stream = NULL;
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
		if (e.type == SDL_EVENT_QUIT) {
			g_app->ret = 1;
		} else if (e.type == SDL_EVENT_KEY_DOWN) {
			S9_LOG_TRACE("key pressed: %s", SDL_GetKeyName(e.key.key));
			switch (e.key.key) {
			case SDLK_1: key_set_mode(S9_MODE_FULL);     break;
			case SDLK_2: key_set_mode(S9_MODE_SCOPE);    break;
			case SDLK_3: key_set_mode(S9_MODE_SPECTRUM); break;
			case SDLK_4: key_set_mode(S9_MODE_PERIODIC); break;
			case SDLK_5: key_set_mode(S9_MODE_SPECTROGRAM); break;
			default: break;
			}
		} else if (e.type == SDL_EVENT_AUDIO_DEVICE_REMOVED && e.adevice.recording && e.adevice.which == g_app->audio_device_id) {
			reopen_audio_input("device removed");
		} else if (e.type == SDL_EVENT_AUDIO_DEVICE_ADDED && e.adevice.recording && g_app->audio_stream == NULL) {
			reopen_audio_input(NULL);
		}
	}
	
	// exit if necessary
	if (g_app->ret) return g_app->ret;

	// the input watched: reopened when its audio stops arriving, tried again while there is none
	uint32_t now = (uint32_t)SDL_GetTicks();
	if (g_app->audio_stream) {
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
