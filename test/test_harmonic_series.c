// The harmonic series estimator, held to the validation oracle's goldens:
// vowels with weak or missing fundamentals, odd harmonics only, a sawtooth,
// a sine off its bin, noise, aliases, hum, a sub-oscillator strong enough to
// be the fundamental and one too weak, an octave pair, a fifth, and frames
// with no series at all.
#include "greatest.h"
#include "harmonic_series.h"
#include "fft_analyzer.h"
#include "test_signals.h"
#include "harmonic_series_golden.h"

#include <math.h>
#include <stdlib.h>

#define VALID 0.5   // harmonicity below which a frame holds no series worth reporting

static harmonic_series_config_t golden_config(void) {
	const harmonic_series_golden_config_t *g = &harmonic_series_golden_config;
	return (harmonic_series_config_t){
		.f_min = (float)g->f_min, .f_max = (float)g->f_max, .peak_floor_db = (float)g->peak_floor_db,
		.prominence_db = (float)g->prominence_db, .shoulder = g->shoulder, .strong_db = (float)g->strong_db,
		.divisors = g->divisors, .coarse_bins = (float)g->coarse_bins, .fine_bins = (float)g->fine_bins,
		.sparse = (float)g->sparse, .completeness = (float)g->completeness, .min_extra = g->min_extra,
		.extra_power = (float)g->extra_power, .fit_floor_db = (float)g->fit_floor_db, .stray_bins = (float)g->stray_bins,
	};
}

// Each golden signal through the analyzer's spectrum gives the oracle's
// fundamental within a thousandth of a cent, its harmonicity, its count of
// partials, and its uncertainty; where the oracle found no series worth
// reporting, neither does the estimator.
TEST estimates_match_golden(void) {
	enum { N = HARMONIC_SERIES_GOLDEN_N, BINS = N / 2 + 1 };
	float *x = malloc(sizeof(float) * N);
	float *db = malloc(sizeof(float) * BINS);
	void *scratch = malloc(harmonic_series_scratch_size(BINS));
	harmonic_series_config_t cfg = golden_config();
	for (unsigned i = 0; i < HARMONIC_SERIES_GOLDEN_CASE_COUNT; i++) {
		const harmonic_series_golden_case_t *g = &harmonic_series_golden_cases[i];
		test_signal_partials(g->partials, g->partial_count, HARMONIC_SERIES_GOLDEN_AMP, g->seed, g->noise_amp, x, N,
		                     HARMONIC_SERIES_GOLDEN_FS);

		fft_analyzer_t *fa = fft_analyzer_new(&(fft_analyzer_config_t){
			.fft_len = N, .window_function = WINDOW_FUNCTION_BLACKMAN_HARRIS, .window_skew = 0.0f, .mode = FFT_ANALYZER_MIX,
		});
		fft_analyzer_prepare_to_stream(fa, &(s9_audio_stream_config_t){
			.audio_format = S9_FORMAT_FLOAT32, .sample_rate = HARMONIC_SERIES_GOLDEN_FS, .channels = 1,
			.buffer_frames = 256, .samples_per_display_frame = N,
		});
		fft_analyzer_enqueue_audio_data(fa, x, sizeof(float) * N);
		fft_analyzer_update(fa);
		harmonic_window_t model;
		harmonic_window_compute(&model, fft_analyzer_window(fa), N);
		harmonic_magnitude_db(fft_analyzer_spectrum(fa, 0), BINS, N, db);

		harmonic_series_t e = harmonic_series_estimate(db, BINS, (double)HARMONIC_SERIES_GOLDEN_FS / N, &model, &cfg, scratch);
		if (g->harmonicity < VALID) {
			ASSERTm(g->label, e.harmonicity < VALID);
		} else {
			ASSERT_IN_RANGEm(g->label, 0.0, 1200.0 * log2((double)e.f0 / g->f0), 0.001);
			ASSERT_IN_RANGEm(g->label, g->harmonicity, (double)e.harmonicity, 1e-3);
			ASSERT_EQm(g->label, g->count, e.count);
			ASSERT_IN_RANGEm(g->label, g->uncertainty_cents, (double)e.uncertainty_cents, 1e-4 + 0.1 * g->uncertainty_cents);
		}
		fft_analyzer_free(&fa);
	}
	free(scratch);
	free(db);
	free(x);
	PASS();
}

// A spectrum too small to hold a peak gives nothing.
TEST too_small_gives_nothing(void) {
	float db[2] = { 0.0f, 0.0f };
	harmonic_window_t model = { { 0 }, { 0 }, 4 };
	char scratch[64];
	harmonic_series_t e = harmonic_series_estimate(db, 2, 1.0, &model, &default_harmonic_series_config, scratch);
	ASSERT_EQ_FMT(0.0f, e.f0, "%g");
	ASSERT_EQ(0u, e.count);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(estimates_match_golden);
	RUN_TEST(too_small_gives_nothing);
	GREATEST_MAIN_END();
}
