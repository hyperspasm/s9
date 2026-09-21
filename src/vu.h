#ifndef vu_h
#define vu_h

#include "s9_dsp.h"
#include "window_function.h"
#include "s9_colors.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

typedef struct vu vu_t;

typedef struct {
	// dsp configuration parameters
	float rms_window_seconds;
	window_function_t rms_window_function;
	
	// drawing configuration parameters
	float min_dbfs;
	float over_threshold_db;
	int margin;
	uint32_t peak_hold_ms;
	SDL_Color rms_color;
	SDL_Color bar_background_color;
	SDL_Color peak_color;
	SDL_Color peak_hold_color;
	SDL_Color peak_over_color;
} vu_config_t;

static const vu_config_t default_vu_config = {
	.rms_window_seconds = 0.25f,
	.rms_window_function = WINDOW_FUNCTION_HAMMING,
	.min_dbfs = -60.f,
	.over_threshold_db = -0.25f,
	.margin = 8,
	.peak_hold_ms = 1000,
	.bar_background_color = { .r = 20, .g = 20, .b = 20, .a = 255 },
	.rms_color = S9_ORANGE,
	.peak_color = S9_HONEYDEW,
	.peak_hold_color = S9_WHITE,
	.peak_over_color = S9_RASPBERRY,
};

// create vu
vu_t *vu_new(const vu_config_t *config);

// destroy vu
void vu_free(vu_t **vu);

// render vu
void vu_render(vu_t *vu, SDL_Renderer *renderer);

// stop audio streaming
void vu_stop_streaming(vu_t *vu);

// prepare for audio streaming
void vu_prepare_to_stream(vu_t *vu, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void vu_enqueue_audio_data(vu_t *vu, const void *data, uint32_t size);

#endif /* vu_h */
