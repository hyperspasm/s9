#ifndef spectrum_h
#define spectrum_h

#include "s9_dsp.h"
#include "fresp_graph.h"
#include "window_function.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

// A live spectrum of one channel, or of the channels averaged: level
// against log frequency, from the newest frame of audio through a window
// skewed toward its end, so that peaks fall as they age, read as the
// largest transform bin within each pixel, so that nothing is missed. The
// frame is as long as it takes for a tone's ridge to be resolution_cents
// wide at the bottom of the range, and narrower above in proportion to
// frequency; its transform is zero-padded so that a tone between the
// frame's own bins reads its level.

typedef struct spectrum spectrum_t;

typedef struct {
	uint32_t channel;                   // the channel shown, or S9_CHANNEL_ALL for their average
	uint32_t resolution_cents;          // the width of a tone's ridge at half amplitude at the bottom of the range
	uint32_t padding;                   // the transform is 2^padding times the frame: the same spectrum sampled
	                                    // that many times as finely, at that much more cost
	window_function_t window_function;
	float window_skew;                  // toward the newest audio: fresher peaks, higher skirts
	fresp_graph_config_t graph_config;  // the range, the dB axis and the colors
} spectrum_config_t;

static const spectrum_config_t default_spectrum_config = {
	.channel = S9_CHANNEL_ALL,
	.resolution_cents = 200,
	.padding = 2,
	.window_function = WINDOW_FUNCTION_HAMMING,
	.window_skew = 1.0f,
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
};

// create spectrum; NULL takes the default configuration
spectrum_t *spectrum_new(const spectrum_config_t *config);

// destroy spectrum
void spectrum_free(spectrum_t **sp);

// render spectrum
void spectrum_render(spectrum_t *sp, SDL_Renderer *renderer);

// the configuration, and its replacement, applied at the next frame
const spectrum_config_t *spectrum_config(const spectrum_t *sp);
void spectrum_set_config(spectrum_t *sp, const spectrum_config_t *config);

// the frame's length in samples, as the range and the resolution set it; 0 before streaming
uint32_t spectrum_frame(const spectrum_t *sp);

// stop audio streaming
void spectrum_stop_streaming(spectrum_t *sp);

// prepare for audio streaming
void spectrum_prepare_to_stream(spectrum_t *sp, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void spectrum_enqueue_audio_data(spectrum_t *sp, const void *data, uint32_t size);

#endif /* spectrum_h */
