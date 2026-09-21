#include "s9.h"
#include "spectrum.h"
#include "fft_analyzer.h"
#include "fft.h"
#include "fresp_graph.h"

#include <math.h>
#include <stdlib.h>

#define FRAME_MIN 1024           // the frame's length, at least
#define FRAME_MAX 524288         // and at most
#define PADDING_MAX 3            // the transform at most 8 times the frame
#define BANDWIDTH_LENGTH 4096    // the length the window's width is measured at
#define FLOOR_POWER 1e-20f       // a level below this is silence, -200 dB, drawn below the plot rather than at infinity

typedef struct spectrum {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	spectrum_config_t config;
	int config_changed;              // applied at the next frame
	double bandwidth;                // the window's, as configured: its width at half amplitude, bins

	fft_analyzer_t *analyzer;
	s9_audio_stream_config_t stream;

	fresp_graph_t *graph;
	char label[64];                  // the channel's name, shown in the plot's corner
} spectrum_t;

// -- configuration

const spectrum_config_t *spectrum_config(const spectrum_t *sp) {
	return &sp->config;
}

void spectrum_set_config(spectrum_t *sp, const spectrum_config_t *config) {
	const spectrum_config_t *d = &default_spectrum_config;
	sp->config = config ? *config : *d;
	if (sp->config.resolution_cents < 1) sp->config.resolution_cents = d->resolution_cents;
	if (sp->config.padding > PADDING_MAX) sp->config.padding = PADDING_MAX;
	if ((unsigned)sp->config.window_function >= WINDOW_FUNCTION_COUNT) sp->config.window_function = d->window_function;
	if (!(sp->config.window_skew >= 0.0f)) sp->config.window_skew = d->window_skew;
	fresp_graph_config_t *g = &sp->config.graph_config;
	if (!(g->min_freq > 0 && g->max_freq > g->min_freq)) { g->min_freq = d->graph_config.min_freq; g->max_freq = d->graph_config.max_freq; }
	if (!(g->max_db > g->min_db)) { g->min_db = d->graph_config.min_db; g->max_db = d->graph_config.max_db; }
	sp->config_changed = 1;
}

// the window's width at half amplitude, with its skew, measured from its shape
static double measure_bandwidth(const spectrum_t *sp) {
	float *w = malloc(sizeof(float) * BANDWIDTH_LENGTH);
	CHECK_ALLOC(w, return window_function_bandwidth(sp->config.window_function));
	create_window_function(sp->config.window_function, BANDWIDTH_LENGTH, 0, sp->config.window_skew, w);
	double width = window_bandwidth(w, BANDWIDTH_LENGTH);
	free(w);
	return width;
}

// The frame: the shortest fast length that holds the window of a ridge
// resolution_cents wide at the bottom of the range, within FRAME_MIN and
// FRAME_MAX.
static uint32_t frame_length(const spectrum_t *sp) {
	double semitones = pow(2.0, (double)sp->config.resolution_cents / 1200.0) - 1.0;
	double need = sp->bandwidth * sp->stream.sample_rate / ((double)sp->config.graph_config.min_freq * semitones);
	if (need < FRAME_MIN) need = FRAME_MIN;
	if (need > FRAME_MAX) need = FRAME_MAX;
	return fft_fast_length((uint32_t)ceil(need));
}

static fft_analyzer_config_t analyzer_config(const spectrum_t *sp) {
	return (fft_analyzer_config_t){
		.fft_len = frame_length(sp),
		.window_function = sp->config.window_function,
		.window_skew = sp->config.window_skew,
		.mode = FFT_ANALYZER_MIX,
		.channel = sp->config.channel,
		.pad = 1u << sp->config.padding,
	};
}

// hand a changed configuration to the analyzer and the graph, and the channel's name to the graph
static void apply_config(spectrum_t *sp) {
	if (!sp->config_changed) return;
	sp->config_changed = 0;
	sp->bandwidth = measure_bandwidth(sp);
	s9_channel_label(sp->config.channel, sp->stream.channels, sp->label, sizeof sp->label);
	if (sp->stream.sample_rate) {
		fft_analyzer_config_t ac = analyzer_config(sp);
		fft_analyzer_set_config(sp->analyzer, &ac);
	}
	if (sp->graph) {
		fresp_graph_set_config(sp->graph, &sp->config.graph_config);
		fresp_graph_set_label(sp->graph, sp->label[0] ? sp->label : NULL);
	}
}

// -- lifecycle

spectrum_t *spectrum_new(const spectrum_config_t *config) {
	spectrum_t *sp = (spectrum_t *) malloc(sizeof(spectrum_t));
	CHECK_ALLOC(sp, return NULL);
	memset(sp, 0, sizeof(*sp));

	spectrum_set_config(sp, config);
	sp->bandwidth = measure_bandwidth(sp);
	sp->config_changed = 0;

	sp->analyzer = fft_analyzer_new(&(fft_analyzer_config_t){
		.fft_len = FRAME_MIN,
		.window_function = sp->config.window_function,
		.window_skew = sp->config.window_skew,
		.mode = FFT_ANALYZER_MIX,
		.channel = sp->config.channel,
		.pad = 1u << sp->config.padding,
	});
	CHECK_ALLOC(sp->analyzer, free(sp); return NULL);

	return sp;
}

void spectrum_free(spectrum_t **sp) {
	if(sp && *sp) {
		fft_analyzer_free(&(*sp)->analyzer);
		fresp_graph_free(&(*sp)->graph);
		free(*sp);
		*sp = NULL;
	}
}

uint32_t spectrum_frame(const spectrum_t *sp) {
	return sp->stream.sample_rate ? frame_length(sp) : 0;
}

static S9_FORCE_INLINE void prepare_to_draw(spectrum_t *sp, SDL_Renderer *renderer) {
	SDL_Rect viewport;
	SDL_GetRenderViewport(renderer, &viewport);

	if(sp->renderer != renderer || !SDL_RectsEqual(&sp->viewport, &viewport)) {
		sp->renderer = renderer;
		sp->viewport = viewport;
		fresp_graph_free(&sp->graph);
		sp->graph = fresp_graph_new(&sp->config.graph_config);
		if (sp->graph) fresp_graph_set_label(sp->graph, sp->label[0] ? sp->label : NULL);
	}
}

// The plot's points from a transform: the largest magnitude among the bins
// within each point's share of the log frequency axis, in dB relative to
// a full-scale sine. The bins are the transform's, spaced by its length;
// the level is the frame's, since the padding adds nothing to a peak.
static void compute_plot_data(const float *fft, uint32_t transform_len, uint32_t frame_len, uint32_t sample_rate,
                              uint32_t min_freq, uint32_t max_freq, float *data, uint32_t data_len) {
	float bin_size_inv = (float)transform_len / (float)sample_rate;
	float log_min = log10f((float)min_freq);
	float log_max = log10f((float)max_freq);
	float inv_frame_len_half = 2.f / (float)frame_len;
	float inv_frame_len_quarter = inv_frame_len_half * inv_frame_len_half;
	float log_range_inv_data_len = (log_max - log_min) / data_len;
	uint32_t half_transform_len = transform_len / 2;

	for (uint32_t i = 0; i < data_len; i++) {
		// the range of the log frequency axis this point covers
		float log_freq_start = log_min + (i * log_range_inv_data_len);
		float log_freq_end = log_min + ((i + 1) * log_range_inv_data_len);
		float freq_start = powf(10.0f, log_freq_start);
		float freq_end = powf(10.0f, log_freq_end);

		// the bins within it
		uint32_t bin_start = (uint32_t)(freq_start * bin_size_inv);
		uint32_t bin_end = (uint32_t)(freq_end * bin_size_inv);
		if (bin_start >= half_transform_len) bin_start = half_transform_len - 1;
		if (bin_end >= half_transform_len) bin_end = half_transform_len - 1;

		// the largest power among them
		float max_power = 0.0f;
		for (uint32_t bin = bin_start; bin <= bin_end; bin++) {
			float real = fft[2 * bin];
			float imag = fft[2 * bin + 1];
			float power = real * real + imag * imag;
			if (power > max_power) max_power = power;
		}

		// scaled to amplitude and in dB: 10 log10 of the power is 20 log10 of the magnitude, sparing the root
		float power = max_power * inv_frame_len_quarter;
		if (power < FLOOR_POWER) power = FLOOR_POWER;
		data[i] = 10.0f * log10f(power);
	}
}

static int fresp_graph_data_callback(void *context, float *plot_data, uint32_t n_points) {
	spectrum_t *sp = context;
	fft_analyzer_t *fa = sp->analyzer;
	compute_plot_data(fft_analyzer_spectrum(fa, 0), fft_analyzer_transform_len(fa), fft_analyzer_fft_len(fa),
	                  fft_analyzer_sample_rate(fa), sp->config.graph_config.min_freq, sp->config.graph_config.max_freq, plot_data, n_points);
	return 0;
}

void spectrum_render(spectrum_t *sp, SDL_Renderer *renderer) {
	if(!fft_analyzer_streaming(sp->analyzer)) return;
	apply_config(sp);
	prepare_to_draw(sp, renderer);
	if (!sp->graph) return;

	fft_analyzer_update(sp->analyzer);
	fresp_graph_render(sp->graph, renderer, fresp_graph_data_callback, sp);
}

void spectrum_stop_streaming(spectrum_t *sp) {
	fft_analyzer_stop_streaming(sp->analyzer);
	sp->renderer = NULL;
}

void spectrum_prepare_to_stream(spectrum_t *sp, const s9_audio_stream_config_t *stream_config) {
	CHECK_PARAMETER(stream_config->sample_rate > 0, return);
	sp->stream = *stream_config;
	sp->config_changed = 1;
	apply_config(sp);
	fft_analyzer_prepare_to_stream(sp->analyzer, stream_config);
}

void spectrum_enqueue_audio_data(spectrum_t *sp, const void *data, uint32_t size) {
	fft_analyzer_enqueue_audio_data(sp->analyzer, data, size);
}
