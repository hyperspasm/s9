#ifndef spectrogram_h
#define spectrogram_h

#include "s9_dsp.h"
#include "fresp_graph.h"
#include "window_function.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

typedef struct spectrogram spectrogram_t;

typedef struct {
	fresp_graph_config_t graph_config;
	float resolution_hz;
	window_function_t window_function;
} spectrogram_config_t;

static const spectrogram_config_t default_spectrogram_config = {
	.graph_config = {
		.min_freq = 20,
		.max_freq = 22000,
		.min_db = -60,
		.max_db = 0,
		.major_db_interval = 10,
		.minor_db_interval = 1,
		.major_color = { .r = 255, .g = 255, .b = 255, .a = 255 },
		.minor_color = { .r = 255, .g = 255, .b = 255, .a = 255 },
	},
		.resolution_hz = 1.f,
		.window_function = WINDOW_FUNCTION_HAMMING,
};

// create spectrogram
spectrogram_t *spectrogram_new(const spectrogram_config_t *config, TTF_Font* (*load_font)(int point_size));

// destroy spectrogram
void spectrogram_free(spectrogram_t **sg);

// render spectrogram
void spectrogram_render(spectrogram_t *sg, SDL_Renderer *renderer);

// prepare for audio streaming
void spectrogram_prepare_to_stream(spectrogram_t *sg, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void spectrogram_enqueue_audio_data(spectrogram_t *sg, const void *data, uint32_t size);

#endif /* spectrogram_h */
