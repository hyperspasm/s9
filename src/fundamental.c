#include "fundamental.h"
#include "s9.h"
#include "fft_analyzer.h"

#include <math.h>
#include <stdlib.h>

struct fundamental {
	fundamental_config_t config;
	int config_changed;              // applied at the next update
	uint32_t sample_rate;            // of the stream; 0 before streaming

	fft_analyzer_t *analyzer;
	period_conditioner_t *conditioner;
	uint32_t buffer_len;             // the transform length db and the scratch are sized for
	float *db;                       // the latest spectrum's magnitudes
	void *scratch;                   // the estimator's
	harmonic_window_t model;         // of the transform's window
	uint32_t model_len;              // the transform length the model is for; 0 before any
	window_function_t model_function;

	bool valid;
	float frequency;
	harmonic_series_t estimate;      // the last update's
};

// the longest power of two of samples within the window
static uint32_t window_points(const fundamental_t *t) {
	uint32_t points = 2;
	while (points * 2 <= t->config.window_s * (float)t->sample_rate) points *= 2;
	return points;
}

static fft_analyzer_config_t analyzer_config(const fundamental_t *t) {
	return (fft_analyzer_config_t){
		.fft_len = t->sample_rate ? window_points(t) : 0,
		.resolution_hz = 1.0f,   // unused: the length is given
		.window_function = t->config.window_function,
		.window_skew = 0.0f,
		.mode = FFT_ANALYZER_MIX,
		.channel = t->config.channel,
		.averaging_s = t->config.averaging_s,
	};
}

const fundamental_config_t *fundamental_config(const fundamental_t *t) {
	return &t->config;
}

void fundamental_set_config(fundamental_t *t, const fundamental_config_t *config) {
	t->config = config ? *config : default_fundamental_config;
	if (!(t->config.window_s > 0.0f)) t->config.window_s = default_fundamental_config.window_s;
	t->config_changed = 1;
}

// hand a changed configuration to the analyzer and the conditioner
static void apply_config(fundamental_t *t) {
	if (!t->config_changed) return;
	t->config_changed = 0;
	fft_analyzer_config_t ac = analyzer_config(t);
	fft_analyzer_set_config(t->analyzer, &ac);
	period_conditioner_set_config(t->conditioner, &t->config.conditioner);
}

fundamental_t *fundamental_new(const fundamental_config_t *config) {
	fundamental_t *t = malloc(sizeof(fundamental_t));
	CHECK_ALLOC(t, return NULL);
	memset(t, 0, sizeof(*t));

	fundamental_set_config(t, config);
	t->config_changed = 0;

	fft_analyzer_config_t ac = analyzer_config(t);
	t->analyzer = fft_analyzer_new(&ac);
	CHECK_ALLOC(t->analyzer, free(t); return NULL);

	t->conditioner = malloc(period_conditioner_alloc_size());
	CHECK_ALLOC(t->conditioner, fundamental_free(&t); return NULL);
	period_conditioner_init(t->conditioner, &t->config.conditioner);
	return t;
}

void fundamental_free(fundamental_t **t) {
	if (t && *t) {
		fft_analyzer_free(&(*t)->analyzer);
		free((*t)->conditioner);
		free((*t)->db);
		free((*t)->scratch);
		free(*t);
		*t = NULL;
	}
}

bool fundamental_valid(const fundamental_t *t) { return t->valid; }
float fundamental_frequency(const fundamental_t *t) { return t->frequency; }
float fundamental_harmonicity(const fundamental_t *t) { return t->estimate.harmonicity; }
uint32_t fundamental_partials(const fundamental_t *t) { return t->estimate.count; }
float fundamental_uncertainty_cents(const fundamental_t *t) { return t->estimate.uncertainty_cents; }

int fundamental_streaming(const fundamental_t *t) { return fft_analyzer_streaming(t->analyzer); }
uint32_t fundamental_fft_len(const fundamental_t *t) { return fft_analyzer_fft_len(t->analyzer); }
uint32_t fundamental_sample_rate(const fundamental_t *t) { return fft_analyzer_sample_rate(t->analyzer); }
const float *fundamental_bins(const fundamental_t *t) { return fft_analyzer_spectrum(t->analyzer, 0); }
const float *fundamental_magnitude_db(const fundamental_t *t) { return t->buffer_len ? t->db : NULL; }
const float *fundamental_power(const fundamental_t *t) { return fft_analyzer_power(t->analyzer, 0); }
float fundamental_average_fraction(const fundamental_t *t) { return fft_analyzer_average_fraction(t->analyzer); }
const float *fundamental_window(const fundamental_t *t) { return fft_analyzer_window(t->analyzer); }
window_function_t fundamental_window_function(const fundamental_t *t) { return fft_analyzer_config(t->analyzer)->window_function; }
float fundamental_hop_seconds(const fundamental_t *t) { return fft_analyzer_hop_seconds(t->analyzer); }

void fundamental_restart_average(fundamental_t *t) { fft_analyzer_reset_power(t->analyzer); }

// -- streaming

void fundamental_prepare_to_stream(fundamental_t *t, const s9_audio_stream_config_t *stream_config) {
	t->sample_rate = stream_config->sample_rate;
	fft_analyzer_config_t ac = analyzer_config(t);
	fft_analyzer_set_config(t->analyzer, &ac);
	fft_analyzer_prepare_to_stream(t->analyzer, stream_config);
	period_conditioner_reset(t->conditioner);
	t->valid = false;
	t->frequency = 0.0f;
	t->estimate = (harmonic_series_t){ 0.0f, 0.0f, 0, 0.0f };
}

void fundamental_stop_streaming(fundamental_t *t) {
	fft_analyzer_stop_streaming(t->analyzer);
}

void fundamental_enqueue_audio_data(fundamental_t *t, const void *data, uint32_t size) {
	fft_analyzer_enqueue_audio_data(t->analyzer, data, size);
}

// -- tracking

void fundamental_update(fundamental_t *t) {
	fft_analyzer_t *fa = t->analyzer;
	if (!fft_analyzer_streaming(fa)) return;
	apply_config(t);
	fft_analyzer_update(fa);
	if (!fft_analyzer_streaming(fa)) return;   // a failed reconfiguration ends the stream

	uint32_t n = fft_analyzer_fft_len(fa), bins = n / 2 + 1;
	float fs = (float)fft_analyzer_sample_rate(fa);
	if (t->buffer_len != n) {
		free(t->db); free(t->scratch);
		t->db = malloc(sizeof(float) * bins);
		t->scratch = malloc(harmonic_series_scratch_size(bins));
		t->buffer_len = t->db && t->scratch ? n : 0;
		CHECK_ALLOC(t->db, return);
		CHECK_ALLOC(t->scratch, return);
	}
	// the estimator's model of the window follows the transform's
	window_function_t function = fft_analyzer_config(fa)->window_function;
	if (t->model_len != n || t->model_function != function) {
		harmonic_window_compute(&t->model, fft_analyzer_window(fa), n);
		t->model_len = n;
		t->model_function = function;
	}

	harmonic_magnitude_db(fft_analyzer_spectrum(fa, 0), bins, n, t->db);
	t->estimate = harmonic_series_estimate(t->db, bins, fs / n, &t->model, &t->config.series, t->scratch);
	float period = t->estimate.f0 > 0.0f ? fs / t->estimate.f0 : 0.0f;
	float rms = fft_analyzer_rms(fa, 0);
	float dt = fft_analyzer_hop_seconds(fa);
	t->valid = period_conditioner_update(t->conditioner, period, t->estimate.harmonicity, rms, fs, dt, &t->frequency);
}
