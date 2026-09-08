// Harmonic readouts and the parabola correction, held to the validation oracle's goldens.
#include "greatest.h"
#include "harmonic_readout.h"
#include "fft_analyzer.h"
#include "window_function.h"
#include "test_signals.h"
#include "harmonics_golden.h"

#include <stdlib.h>

// The bias tables for the goldens' windows.
TEST correction_tables_match_golden(void) {
	struct { window_function_t function; const double *db_bias, *offset_bias; const char *label; } windows[] = {
		{ WINDOW_FUNCTION_HAMMING, harmonics_golden_hamming_db_bias, harmonics_golden_hamming_offset_bias, "hamming" },
		{ WINDOW_FUNCTION_BLACKMAN_HARRIS, harmonics_golden_blackman_harris_db_bias, harmonics_golden_blackman_harris_offset_bias, "blackman_harris" },
	};
	float *w = malloc(sizeof(float) * HARMONICS_GOLDEN_N);
	for (unsigned i = 0; i < 2; i++) {
		create_window_function(windows[i].function, HARMONICS_GOLDEN_N, 1, 0.0f, w);
		harmonic_correction_t c;
		harmonic_correction_compute(&c, w, HARMONICS_GOLDEN_N);
		for (int g = 0; g < HARMONIC_CORRECTION_POINTS; g++) {
			ASSERT_IN_RANGEm(windows[i].label, windows[i].db_bias[g], c.db_bias[g], 1e-6);
			ASSERT_IN_RANGEm(windows[i].label, windows[i].offset_bias[g], c.offset_bias[g], 1e-6);
		}
	}
	free(w);
	PASS();
}

// whether the signal contains harmonic k of its fundamental
static int has_harmonic(const harmonics_golden_case_t *g, unsigned k) {
	switch (g->kind) {
	case SIGNAL_SINE:     return k == 1;
	case SIGNAL_SAWTOOTH: return k <= g->harmonics;
	case SIGNAL_SQUARE:   return k <= g->harmonics && k % 2 == 1;
	default:              return 0;
	}
}

// The readouts of each golden signal's harmonics, through the analyzer's
// spectrum. Where the signal has no harmonic, only leakage and rounding
// noise are read; that reading is held to be quiet rather than to the
// oracle's exact value.
TEST readouts_match_golden(void) {
	enum { N = HARMONICS_GOLDEN_N, BINS = N / 2 + 1 };
	float *x = malloc(sizeof(float) * N);
	float *db = malloc(sizeof(float) * BINS);
	for (unsigned i = 0; i < HARMONICS_GOLDEN_CASE_COUNT; i++) {
		const harmonics_golden_case_t *g = &harmonics_golden_cases[i];
		test_signal_t s = { g->kind, g->freq, g->freq2, g->amp, g->phase, g->harmonics, g->seed };
		test_signal_generate(&s, x, N, HARMONICS_GOLDEN_FS);

		fft_analyzer_t *fa = fft_analyzer_new(&(fft_analyzer_config_t){
			.fft_len = N, .window_function = (window_function_t)g->window, .window_skew = (float)g->skew, .mode = FFT_ANALYZER_MIX,
		});
		fft_analyzer_prepare_to_stream(fa, &(s9_audio_stream_config_t){
			.audio_format = S9_FORMAT_FLOAT32, .sample_rate = HARMONICS_GOLDEN_FS, .channels = 1,
			.buffer_frames = 256, .samples_per_display_frame = N,
		});
		fft_analyzer_enqueue_audio_data(fa, x, sizeof(float) * N);
		fft_analyzer_update(fa);

		harmonic_correction_t c;
		harmonic_correction_compute(&c, fft_analyzer_window(fa), N);
		harmonic_magnitude_db((const float *)fft_analyzer_spectrum(fa, 0), BINS, N, db);

		for (unsigned k = 1; k <= g->count; k++) {
			harmonic_readout_t r = harmonic_readout(db, BINS, (double)HARMONICS_GOLDEN_FS / N, k * g->f0, HARMONICS_GOLDEN_SEARCH, &c);
			if (has_harmonic(g, k)) {
				ASSERT_EQm(g->label, g->found[k - 1], r.found);
				ASSERT_IN_RANGEm(g->label, g->frequency[k - 1], (double)r.frequency, 2e-3);
				ASSERT_IN_RANGEm(g->label, g->db[k - 1], (double)r.db, 1e-4);
			} else if (r.found) {
				ASSERTm(g->label, r.db < -40.0f);
			}
		}
		fft_analyzer_free(&fa);
	}
	free(db);
	free(x);
	PASS();
}

// A full-scale sine on a bin centre reads 0 dB at its own frequency, and
// harmonics beyond the last usable bin are not found.
TEST calibration_and_range(void) {
	enum { N = 1024, BINS = N / 2 + 1, BIN = 64 };
	float *x = malloc(sizeof(float) * N), *db = malloc(sizeof(float) * BINS);
	test_signal_t s = { SIGNAL_SINE, (double)BIN * 44100 / N, 0, 1.0, 0.7, 0, 0 };
	test_signal_generate(&s, x, N, 44100);

	fft_analyzer_t *fa = fft_analyzer_new(&(fft_analyzer_config_t){ .fft_len = N, .window_function = WINDOW_FUNCTION_BLACKMAN_HARRIS, .mode = FFT_ANALYZER_MIX });
	fft_analyzer_prepare_to_stream(fa, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = 44100, .channels = 1, .buffer_frames = 256, .samples_per_display_frame = N,
	});
	fft_analyzer_enqueue_audio_data(fa, x, sizeof(float) * N);
	fft_analyzer_update(fa);
	harmonic_correction_t c;
	harmonic_correction_compute(&c, fft_analyzer_window(fa), N);
	harmonic_magnitude_db((const float *)fft_analyzer_spectrum(fa, 0), BINS, N, db);

	double delta_f = 44100.0 / N;
	harmonic_readout_t r = harmonic_readout(db, BINS, delta_f, BIN * delta_f, 3, &c);
	ASSERT(r.found);
	ASSERT_IN_RANGE(BIN * delta_f, (double)r.frequency, 1e-3);
	ASSERT_IN_RANGE(0.0, (double)r.db, 1e-3);
	ASSERT(!harmonic_readout(db, BINS, delta_f, (N / 2) * delta_f, 3, &c).found);
	ASSERT(!harmonic_readout(db, BINS, delta_f, 0.0, 3, &c).found);

	free(x); free(db);
	fft_analyzer_free(&fa);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(correction_tables_match_golden);
	RUN_TEST(readouts_match_golden);
	RUN_TEST(calibration_and_range);
	GREATEST_MAIN_END();
}
