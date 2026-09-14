#ifndef spectrum_h
#define spectrum_h

#include "s9_dsp.h"
#include "fresp_graph.h"
#include "window_function.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

typedef struct spectrum spectrum_t;

typedef struct {
	fresp_graph_config_t graph_config;
	float resolution_hz;
	window_function_t window_function;
	float window_skew;
} spectrum_config_t;

static const spectrum_config_t default_spectrum_config = {
	.graph_config = {
		.min_freq = 20,
		.max_freq = 22000,
		.min_db = -60,
		.max_db = 0,
		.major_db_interval = 10,
		.minor_db_interval = 1,
		.major_color = S9_WHITE,
		.minor_color = S9_WHITE,
		.plot_color = S9_HONEYDEW,
	},
		.resolution_hz = 2.f,
		.window_function = WINDOW_FUNCTION_HAMMING,
		.window_skew = 1.0f,
};

// create spectrum
spectrum_t *spectrum_new(const spectrum_config_t *config);

// destroy spectrum
void spectrum_free(spectrum_t **sp);

// render spectrum
void spectrum_render(spectrum_t *sp, SDL_Renderer *renderer);

// stop audio streaming
void spectrum_stop_streaming(spectrum_t *sp);

// prepare for audio streaming
void spectrum_prepare_to_stream(spectrum_t *sp, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void spectrum_enqueue_audio_data(spectrum_t *sp, const void *data, uint32_t size);

#endif /* spectrum_h */
