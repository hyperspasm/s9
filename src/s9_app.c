#include "s9_app.h"
#include "Roboto-Regular.h"
#include "fps.h"
#include "vu.h"
#include "spectrogram.h"
#include "oscilloscope.h"
#include "lissajous.h"
#include "scratchpad.h"

#include "s9_pn.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_audio.h>

typedef struct s9_layout {
	uint32_t margin_left, margin_top, margin_right, margin_bottom;
	uint32_t fixed_width;   // >0 ignores margin_right; 0 fills remaining
	uint32_t fixed_height;  // >0 ignores margin_bottom; 0 fills remaining
} s9_layout_t;

typedef struct s9_component {
	void *instance;
	s9_layout_t layout;

	void (*destroy)(void **instance);
	void (*render)(void *instance, SDL_Renderer *renderer);
	void (*enqueue_audio)(void *instance, uint8_t *stream, int len);
	void (*prepare_to_stream)(void *instance, const s9_audio_stream_config_t *config);
} s9_component_t;

static SDL_Rect compute_viewport(const s9_layout_t *layout, const SDL_Rect *parent) {
	SDL_Rect vp;
	vp.x = parent->x + layout->margin_left;
	vp.y = parent->y + layout->margin_top;
	vp.w = layout->fixed_width  ? layout->fixed_width  : parent->w - layout->margin_left - layout->margin_right;
	vp.h = layout->fixed_height ? layout->fixed_height : parent->h - layout->margin_top - layout->margin_bottom;
	return vp;
}

#define S9_MAX_COMPONENTS 256

typedef struct s9_app {
	s9_app_config_t config;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_RendererInfo renderer_info;
	SDL_DisplayMode display_mode;

	s9_component_t components[S9_MAX_COMPONENTS]; // NULL-terminated (instance == NULL)
	s9_pn_t *pn;

	SDL_AudioDeviceID audio_device_id;
	SDL_AudioSpec audio_spec;
	s9_audio_stream_config_t stream_config;
	int streaming;

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

// static function to load a font
static TTF_Font* load_font_from_memory(const uint8_t *font_data, int data_size, int point_size) {
	// create an SDL_RWops from the font data
	SDL_RWops *rw = SDL_RWFromConstMem(font_data, data_size);
	CHECK_ERROR(rw != NULL, return NULL, "SDL_RWFromConstMem failed: %s", SDL_GetError());
	
	// load the font from the SDL_RWops
	TTF_Font *font = TTF_OpenFontRW(rw, 1, point_size); // 1 means SDL will free the RWops
	CHECK_ERROR(font != NULL, return NULL, "TTF_OpenFontRW failed: %s", TTF_GetError());
	
	return font;
}

static TTF_Font* load_font(int point_size) {
	return load_font_from_memory(Roboto_Regular_TTF, sizeof(Roboto_Regular_TTF), point_size);
}

static void render(SDL_Renderer *renderer) {
	SDL_Rect default_viewport;
	SDL_RenderGetViewport(renderer, &default_viewport);

	for (s9_component_t *c = g_app->components; c->instance; c++) {
		if (c->render) {
			SDL_Rect viewport = compute_viewport(&c->layout, &default_viewport);
			SDL_RenderSetViewport(renderer, &viewport);
			c->render(c->instance, renderer);
		}
	}

	SDL_RenderSetViewport(renderer, &default_viewport);
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
	(void)userdata;

	for (s9_component_t *c = g_app->components; c->instance; c++) {
		if (c->enqueue_audio) {
			c->enqueue_audio(c->instance, stream, len);
		}
	}
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
	
	// initialize SDL
	ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO); 
	CHECK_ERROR(ret == 0,
				free(g_app);
				g_app = NULL;
				return -1,
				"SDL_Init failed: %s", SDL_GetError());
	
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
	s9_component_t *component = g_app->components;

	// fps
	fps_t *fps = fps_new(g_app->renderer, load_font, (SDL_Color){255, 255, 255, 255}, 1000);
	CHECK_ERROR(fps != NULL,
				SDL_DestroyRenderer(g_app->renderer);
				SDL_DestroyWindow(g_app->window);
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"Failed to create fps instance");
	*component++ = (s9_component_t){
		.instance = fps,
		.layout = {
			.margin_left = 10,
			.margin_top = 10,
			.margin_right = 0,
			.margin_bottom = 0,
			.fixed_width = 50,
			.fixed_height = 40,
		},
		.destroy = (void (*)(void **))fps_free,
		.render = (void (*)(void *, SDL_Renderer *))fps_render,
	};

	// vu
	vu_t *vu = vu_new(NULL);
	CHECK_ERROR(vu != NULL,
				s9_app_destroy();
				return -1,
				"Failed to create vu instance");
	*component++ = (s9_component_t){
		.instance = vu,
		.layout = {
			.margin_left = 10,
			.margin_top = 40,
			.margin_right = 0,
			.margin_bottom = 10,
			.fixed_width = 50,
			.fixed_height = 0,
		},
		.destroy = (void (*)(void **))vu_free,
		.render = (void (*)(void *, SDL_Renderer *))vu_render,
		.enqueue_audio = (void (*)(void *, uint8_t *, int))vu_enqueue_audio_data,
		.prepare_to_stream = (void (*)(void *, const s9_audio_stream_config_t *))vu_prepare_to_stream,
	};

	// spectrogram
	spectrogram_t *sg = spectrogram_new(NULL, load_font);
	CHECK_ERROR(sg != NULL,
				s9_app_destroy();
				return -1,
				"Failed to create spectrogram instance");
	*component++ = (s9_component_t){
		.instance = sg,
		.layout = {
			.margin_left = 60,
			.margin_top = 0,
			.margin_right = 0,
			.margin_bottom = 0,
			.fixed_width = 860,
			.fixed_height = 0,
		},
		.destroy = (void (*)(void **))spectrogram_free,
		.render = (void (*)(void *, SDL_Renderer *))spectrogram_render,
		.enqueue_audio = (void (*)(void *, uint8_t *, int))spectrogram_enqueue_audio_data,
		.prepare_to_stream = (void (*)(void *, const s9_audio_stream_config_t *))spectrogram_prepare_to_stream,
	};

	// oscilloscope
	oscilloscope_t *osc = oscilloscope_new(NULL);
	CHECK_ERROR(osc != NULL,
				s9_app_destroy();
				return -1,
				"Failed to create oscilloscope instance");
	*component++ = (s9_component_t){
		.instance = osc,
		.layout = {
			.margin_left = 920,
			.margin_top = 0,
			.margin_right = 0,
			.margin_bottom = 0,
			.fixed_width = 0,
			.fixed_height = 360,
		},
		.destroy = (void (*)(void **))oscilloscope_free,
		.render = (void (*)(void *, SDL_Renderer *))oscilloscope_render,
		.enqueue_audio = (void (*)(void *, uint8_t *, int))oscilloscope_enqueue_audio_data,
		.prepare_to_stream = (void (*)(void *, const s9_audio_stream_config_t *))oscilloscope_prepare_to_stream,
	};
	
	// lissajous
	lissajous_t *lissajous = lissajous_new(NULL);
	CHECK_ERROR(lissajous != NULL,
				s9_app_destroy();
				return -1,
				"Failed to create lissajous instance");
	*component++ = (s9_component_t){
		.instance = lissajous,
		.layout = {
			.margin_left = 920,
			.margin_top = 360,
			.margin_right = 0,
			.margin_bottom = 0,
			.fixed_width = 0,
			.fixed_height = 360,
		},
		.destroy = (void (*)(void **))lissajous_free,
		.render = (void (*)(void *, SDL_Renderer *))lissajous_render,
		.enqueue_audio = (void (*)(void *, uint8_t *, int))lissajous_enqueue_audio_data,
		.prepare_to_stream = (void (*)(void *, const s9_audio_stream_config_t *))lissajous_prepare_to_stream,
	};

#if 0
	// scratchpad
	scratchpad_t *scratchpad = scratchpad_new(load_font);
	CHECK_ERROR(scratchpad != NULL,
				s9_app_destroy();
				return -1,
				"Failed to create scratchpad instance");
	*component++ = (s9_component_t){
		.instance = scratchpad,
		.layout = {
			.margin_left = 60,
			.margin_top = 0,
			.margin_right = 0,
			.margin_bottom = 0,
			.fixed_width = 0,
			.fixed_height = 0,
		},
		.destroy = (void (*)(void **))scratchpad_free,
		.render = (void (*)(void *, SDL_Renderer *))scratchpad_render,
	};
#endif

	// create pn
	g_app->pn = s9_pn_new(&(s9_pn_config_t){
		.osc = osc,
	});
	CHECK_ALLOC(g_app->pn, s9_app_destroy(); return -1);

	// check for audio input device
	int num_audio_devices = SDL_GetNumAudioDevices(SDL_TRUE);
	CHECK_ERROR(num_audio_devices > 0,
				SDL_DestroyRenderer(g_app->renderer);
				SDL_DestroyWindow(g_app->window);
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"No available audio input devices");
	
	// choose first device as default
	const char *selected_audio_device_name = SDL_GetAudioDeviceName(0, SDL_TRUE);
	
	// enumerate audio devices and select the specified one, if any
	S9_LOG_INFO("Available audio input devices:");
	for (int i = 0; i < num_audio_devices; i++) {
		const char *device_name = SDL_GetAudioDeviceName(i, SDL_TRUE);
		S9_LOG_INFO("    %s", device_name);
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
	CHECK_ERROR(g_app->audio_device_id > 0,
				SDL_DestroyRenderer(g_app->renderer);
				SDL_DestroyWindow(g_app->window);
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"Failed to open audio device: %s", SDL_GetError());
	
	// print audio device info
	S9_LOG_INFO("Selected audio input device: %s", selected_audio_device_name);
	S9_LOG_INFO("    Sample Rate: %d Hz", g_app->audio_spec.freq);
	S9_LOG_INFO("    Channels: %d", g_app->audio_spec.channels);
	S9_LOG_INFO("    Buffer Size: %d samples", g_app->audio_spec.samples);
	
	// init stream_config
	g_app->stream_config = (s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32,
		.sample_rate = g_app->audio_spec.freq,
		.channels = g_app->audio_spec.channels,
		.buffer_frames = g_app->audio_spec.samples,
		.samples_per_display_frame = ceil(g_app->audio_spec.freq / g_app->display_mode.refresh_rate),
	};
		
	// start streaming audio
	SDL_PauseAudioDevice(g_app->audio_device_id, SDL_FALSE);
	return 0;
}

void s9_app_destroy(void) {
	CHECK_ERROR(g_app != NULL, return, "ignoring attempt to destroy uncreated app");

	// destroy components
	for (s9_component_t *c = g_app->components; c->instance; c++) {
		if (c->destroy) {
			c->destroy(&c->instance);
		}
	}

	// close audio device
	if (g_app->audio_device_id > 0) {
		SDL_PauseAudioDevice(g_app->audio_device_id, SDL_TRUE);
		SDL_CloseAudioDevice(g_app->audio_device_id);
	}
	
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

	// prepare to stream if necessary
	if (g_app->streaming == 0) {
		
		SDL_LockAudioDevice(g_app->audio_device_id);
		for (s9_component_t *c = g_app->components; c->instance; c++) {
			if (c->prepare_to_stream) {
				c->prepare_to_stream(c->instance, &g_app->stream_config);
			}
		}		
		SDL_UnlockAudioDevice(g_app->audio_device_id);

		g_app->streaming = 1;
	}
	
	// poll until all events are handled!
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT) {
			g_app->ret = 1;
		} else if (e.type == SDL_KEYDOWN) {
			S9_LOG_TRACE("key pressed: %s", SDL_GetKeyName(e.key.keysym.sym));
		}
	}
	
	// exit if necessary
	if (g_app->ret) return g_app->ret;
	
	// poll pn
	s9_pn_update(g_app->pn);

	// reset viewport to default
	SDL_RenderSetViewport(g_app->renderer, NULL);
	
	// clear screen
	SDL_SetRenderDrawColor(g_app->renderer, 0, 0, 0, 255);
	SDL_RenderClear(g_app->renderer);
	
	// perform all rendering
	render(g_app->renderer);
	
	// present renderer
	SDL_RenderPresent(g_app->renderer);
	
	return 0;
}
