#include "s9_app.h"
#include "Roboto-Regular.h"
#include "fps.h"
#include "vu.h"
#include "scratchpad.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_audio.h>

typedef struct s9_app {
	s9_app_config_t config;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_RendererInfo renderer_info;
	SDL_DisplayMode display_mode;
	TTF_Font *small_font;
	
	fps_t *fps;
	vu_t *vu;
	scratchpad_t *scratchpad;
	
	SDL_AudioDeviceID audio_device_id;
	SDL_AudioSpec audio_spec;
	s9_audio_stream_config_t stream_config;
	
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

static void render(SDL_Renderer *renderer) {
	SDL_Rect default_viewport;		
	SDL_RenderGetViewport(renderer, &default_viewport);
	
	// scratchpad
	if(g_app->scratchpad) {
		int margin_x = 200;
		SDL_Rect viewport = {
			.x = default_viewport.x + margin_x,
			.y = default_viewport.y,
			.w = default_viewport.w - margin_x,
			.h = default_viewport.h,
		};
		SDL_RenderSetViewport(renderer, &viewport);
		scratchpad_render(g_app->scratchpad, g_app->renderer);		
	}
	
	// fps
	if(g_app->fps) {
		SDL_RenderSetViewport(renderer, &default_viewport);		
		fps_render(g_app->fps, renderer);
	}
	
	// vu
	if(g_app->vu) {
		SDL_RenderSetViewport(renderer, &default_viewport);		
		vu_render(g_app->vu, renderer);
	}
}

// empty audio callback
static void audio_callback(void *userdata, Uint8 *stream, int len) {
	if(g_app->vu) {
		vu_enqueue_audio_data(g_app->vu, stream, len);
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
	
	// load small font
	g_app->small_font = load_font_from_memory(Roboto_Regular_TTF, sizeof(Roboto_Regular_TTF), 16);
	CHECK_ERROR(g_app->small_font != NULL,
				SDL_DestroyRenderer(g_app->renderer);
				SDL_DestroyWindow(g_app->window);
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"Failed to load small font");
	
	// enable blending mode for anti-aliasing
	SDL_SetRenderDrawBlendMode(g_app->renderer, SDL_BLENDMODE_BLEND);
	
	// create scratchpad instance
	g_app->scratchpad = scratchpad_new();
	CHECK_ERROR(g_app->scratchpad != NULL,
				TTF_CloseFont(g_app->small_font);
				SDL_DestroyRenderer(g_app->renderer);
				SDL_DestroyWindow(g_app->window);
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"Failed to create scratchpad instance");
	
	// create fps instance
	g_app->fps = fps_new(g_app->renderer, g_app->small_font, (SDL_Color){255, 255, 255, 255}, 1000);
	CHECK_ERROR(g_app->fps != NULL,
				TTF_CloseFont(g_app->small_font);
				SDL_DestroyRenderer(g_app->renderer);
				SDL_DestroyWindow(g_app->window);
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"Failed to create fps instance");
	
	// create vu instance
	g_app->vu = vu_new();
	CHECK_ERROR(g_app->vu != NULL,
				TTF_CloseFont(g_app->small_font);
				SDL_DestroyRenderer(g_app->renderer);
				SDL_DestroyWindow(g_app->window);
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"Failed to create vu instance");
	
	// check for audio input device
	int num_audio_devices = SDL_GetNumAudioDevices(SDL_TRUE);
	CHECK_ERROR(num_audio_devices > 0,
				TTF_CloseFont(g_app->small_font);
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
	desired_spec.samples = 512;
	desired_spec.callback = audio_callback;
	
	g_app->audio_device_id = SDL_OpenAudioDevice(selected_audio_device_name, SDL_TRUE, &desired_spec, &g_app->audio_spec, 0);
	CHECK_ERROR(g_app->audio_device_id > 0,
				TTF_CloseFont(g_app->small_font);
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
	
	// prepare to stream
	if(g_app->vu) {
		vu_prepare_to_stream(g_app->vu, &g_app->stream_config);
	}
	
	// start streaming audio
	SDL_PauseAudioDevice(g_app->audio_device_id, SDL_FALSE);
	
	return 0;
}

void s9_app_destroy(void) {
	CHECK_ERROR(g_app != NULL, return, "ignoring attempt to destroy uncreated app");
	
	// close font
	if (g_app->small_font) {
		TTF_CloseFont(g_app->small_font);
		g_app->small_font = NULL;
	}
	
	// destroy scratchpad instance
	if (g_app->scratchpad) {
		scratchpad_free(&g_app->scratchpad);
	}
	
	// destroy fps instance
	if (g_app->fps) {
		fps_free(&g_app->fps);
	}
	
	// destroy vu instance
	if (g_app->vu) {
		vu_free(&g_app->vu);
	}
	
	// close audio device
	if (g_app->audio_device_id > 0) {
		SDL_PauseAudioDevice(g_app->audio_device_id, SDL_TRUE);
		SDL_CloseAudioDevice(g_app->audio_device_id);
	}
	
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
		}
	}
	
	// exit if necessary
	if (g_app->ret) return g_app->ret;
	
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
