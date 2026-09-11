// Harmonic readouts, the window model, and the distortion, held to the
// validation oracle's goldens.
#include "greatest.h"
#include "harmonic_readout.h"
#include "fft_analyzer.h"
#include "window_function.h"
#include "test_signals.h"
#include "harmonics_golden.h"

#include <stdlib.h>

// The bias tables and the lobe's reach for the goldens' windows.
TEST window_models_match_golden(void) {
	struct { window_function_t function; const double *db_bias, *offset_bias; unsigned reach; const char *label; } windows[] = {
		{ WINDOW_FUNCTION_HAMMING, harmonics_golden_hamming_db_bias, harmonics_golden_hamming_offset_bias,
		  HARMONICS_GOLDEN_HAMMING_REACH, "hamming" },
		{ WINDOW_FUNCTION_BLACKMAN_HARRIS, harmonics_golden_blackman_harris_db_bias, harmonics_golden_blackman_harris_offset_bias,
		  HARMONICS_GOLDEN_BLACKMAN_HARRIS_REACH, "blackman_harris" },
	};
	float *w = malloc(sizeof(float) * HARMONICS_GOLDEN_N);
	for (unsigned i = 0; i < 2; i++) {
		create_window_function(windows[i].function, HARMONICS_GOLDEN_N, 1, 0.0f, w);
		harmonic_window_t model;
		harmonic_window_compute(&model, w, HARMONICS_GOLDEN_N);
		for (int g = 0; g < HARMONIC_CORRECTION_POINTS; g++) {
			ASSERT_IN_RANGEm(windows[i].label, windows[i].db_bias[g], model.db_bias[g], 1e-6);
			ASSERT_IN_RANGEm(windows[i].label, windows[i].offset_bias[g], model.offset_bias[g], 1e-6);
		}
		ASSERT_EQm(windows[i].label, windows[i].reach, model.reach);
	}
	free(w);
	PASS();
}

// the difference between two angles in degrees, in (-180, 180]
static double angle_diff(double a, double b) {
	double d = fmod(a - b, 360.0);
	if (d < -180.0) d += 360.0;
	if (d > 180.0) d -= 360.0;
	return d;
}

// The readouts and the distortion of each golden signal, through the
// analyzer's spectrum: which harmonics are found, in noise too, and what
// they read.
TEST readouts_match_golden(void) {
	enum { N = HARMONICS_GOLDEN_N, BINS = N / 2 + 1 };
	float *x = malloc(sizeof(float) * N);
	float *db = malloc(sizeof(float) * BINS);
	for (unsigned i = 0; i < HARMONICS_GOLDEN_CASE_COUNT; i++) {
		const harmonics_golden_case_t *g = &harmonics_golden_cases[i];
		test_signal_t s = { g->kind, g->freq, g->freq2, g->amp, g->phase, g->harmonics, g->seed, g->noise_amp };
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

		harmonic_window_t model;
		harmonic_window_compute(&model, fft_analyzer_window(fa), N);
		harmonic_magnitude_db((const float *)fft_analyzer_spectrum(fa, 0), BINS, N, db);

		double delta_f = (double)HARMONICS_GOLDEN_FS / N;
		uint32_t shoulder = harmonic_shoulder(g->f0 / delta_f, model.reach, HARMONICS_GOLDEN_SHOULDER);
		harmonic_readout_t reads[64];
		for (unsigned k = 1; k <= g->count; k++) {
			reads[k - 1] = harmonic_readout(db, BINS, delta_f, k * g->f0, HARMONICS_GOLDEN_SEARCH, shoulder,
											(float)HARMONICS_GOLDEN_PROMINENCE, &model);
			ASSERT_EQm(g->label, g->found[k - 1], reads[k - 1].found);
			if (reads[k - 1].found) {
				ASSERTm(g->label, reads[k - 1].peak && reads[k - 1].prominence >= HARMONICS_GOLDEN_PROMINENCE);
				ASSERT_IN_RANGEm(g->label, g->frequency[k - 1], (double)reads[k - 1].frequency, 2e-3);
				ASSERT_IN_RANGEm(g->label, g->db[k - 1], (double)reads[k - 1].db, 1e-4);
			}
		}

		// the phases, relative to the fundamental, where both were found
		if (reads[0].found) {
			uint32_t p_1 = (uint32_t)lrint(reads[0].frequency / delta_f);
			for (unsigned k = 1; k <= g->count; k++) {
				if (!reads[k - 1].found) continue;
				uint32_t p_k = (uint32_t)lrint(reads[k - 1].frequency / delta_f);
				float phase = harmonic_phase((const float *)fft_analyzer_spectrum(fa, 0), N, p_k, p_1, k);
				ASSERT_IN_RANGEm(g->label, 0.0, angle_diff(phase, g->phase_deg[k - 1]), 0.01);
			}
		}

		float thd;
		int has_thd = harmonic_thd(reads, g->count, &thd);
		ASSERT_EQm(g->label, g->thd >= 0.0, has_thd == 0);
		if (has_thd == 0) ASSERT_IN_RANGEm(g->label, g->thd, (double)thd, g->thd * 1e-4 + 1e-7);

		fft_analyzer_free(&fa);
	}
	free(db);
	free(x);
	PASS();
}

// The shoulder shortens to fit between close harmonics, and never below a bin.
TEST shoulder_fits_between_harmonics(void) {
	ASSERT_EQ(32u, harmonic_shoulder(200.0, 4, 32));
	ASSERT_EQ(6u, harmonic_shoulder(20.4, 4, 32));
	ASSERT_EQ(1u, harmonic_shoulder(8.0, 4, 32));
	PASS();
}

// A full-scale sine on a bin centre reads 0 dB at its own frequency, and
// harmonics beyond the last usable bin are not found.
TEST calibration_and_range(void) {
	enum { N = 1024, BINS = N / 2 + 1, BIN = 64 };
	float *x = malloc(sizeof(float) * N), *db = malloc(sizeof(float) * BINS);
	test_signal_t s = { SIGNAL_SINE, (double)BIN * 44100 / N, 0, 1.0, 0.7, 0, 0, 0 };
	test_signal_generate(&s, x, N, 44100);

	fft_analyzer_t *fa = fft_analyzer_new(&(fft_analyzer_config_t){ .fft_len = N, .window_function = WINDOW_FUNCTION_BLACKMAN_HARRIS, .mode = FFT_ANALYZER_MIX });
	fft_analyzer_prepare_to_stream(fa, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = 44100, .channels = 1, .buffer_frames = 256, .samples_per_display_frame = N,
	});
	fft_analyzer_enqueue_audio_data(fa, x, sizeof(float) * N);
	fft_analyzer_update(fa);
	harmonic_window_t model;
	harmonic_window_compute(&model, fft_analyzer_window(fa), N);
	harmonic_magnitude_db((const float *)fft_analyzer_spectrum(fa, 0), BINS, N, db);

	double delta_f = 44100.0 / N;
	harmonic_readout_t r = harmonic_readout(db, BINS, delta_f, BIN * delta_f, 3, 8, 15.0f, &model);
	ASSERT(r.found);
	ASSERT_IN_RANGE(BIN * delta_f, (double)r.frequency, 1e-3);
	ASSERT_IN_RANGE(0.0, (double)r.db, 1e-3);
	ASSERT(!harmonic_readout(db, BINS, delta_f, (N / 2) * delta_f, 3, 8, 15.0f, &model).found);
	ASSERT(!harmonic_readout(db, BINS, delta_f, 0.0, 3, 8, 15.0f, &model).found);

	free(x); free(db);
	fft_analyzer_free(&fa);
	PASS();
}

// Distortion is the harmonics' RMS over the fundamental's, from the
// readouts found: two harmonics at -20 dB give 14.1%, and none without a
// fundamental.
TEST thd_from_readouts(void) {
	harmonic_readout_t reads[4] = { { 1, 1, 40, 100, -6 }, { 1, 1, 40, 200, -26 }, { 0, 0, 0, 0, 0 }, { 1, 1, 40, 400, -26 } };
	float thd;
	ASSERT_EQ(0, harmonic_thd(reads, 4, &thd));
	ASSERT_IN_RANGE(0.1f * sqrtf(2.0f), thd, 1e-6f);
	reads[0].found = 0;
	ASSERT_EQ(-1, harmonic_thd(reads, 4, &thd));
	PASS();
}

// The gate opens at the on threshold, holds through dips down to the off
// threshold, and closes only after the hold time below that.
TEST gate_has_hysteresis_and_hold(void) {
	harmonic_gate_t g = { 0 };
	harmonic_readout_t r = { 1, 0, 0.0f, 0.0f, 0.0f };
	float on = 10.0f, off = 7.0f, hold = 0.5f, dt = 0.1f;

	r.prominence = 9.0f;
	ASSERT_FALSE(harmonic_gate_update(&g, &r, on, off, hold, dt));   // never reached on
	r.prominence = 10.0f;
	ASSERT(harmonic_gate_update(&g, &r, on, off, hold, dt));
	r.prominence = 7.0f;
	ASSERT(harmonic_gate_update(&g, &r, on, off, hold, dt));         // a dip within hysteresis
	r.prominence = 2.0f;
	for (int i = 0; i < 5; i++) ASSERT(harmonic_gate_update(&g, &r, on, off, hold, dt));   // held for 0.5 s
	ASSERT_FALSE(harmonic_gate_update(&g, &r, on, off, hold, dt));   // then closed
	r.prominence = 8.0f;
	ASSERT_FALSE(harmonic_gate_update(&g, &r, on, off, hold, dt));   // off is not enough to reopen

	r.prominence = 12.0f;
	ASSERT(harmonic_gate_update(&g, &r, on, off, hold, dt));
	r.peak = 0;
	r.prominence = -INFINITY;
	ASSERT(harmonic_gate_update(&g, &r, on, off, hold, 0.4f));       // no peak at all counts as below
	ASSERT_FALSE(harmonic_gate_update(&g, &r, on, off, hold, 0.2f));
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(window_models_match_golden);
	RUN_TEST(shoulder_fits_between_harmonics);
	RUN_TEST(readouts_match_golden);
	RUN_TEST(calibration_and_range);
	RUN_TEST(thd_from_readouts);
	RUN_TEST(gate_has_hysteresis_and_hold);
	GREATEST_MAIN_END();
}
