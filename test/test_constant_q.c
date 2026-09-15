// The constant-Q readout, held to the validation oracle: the bandwidth of
// every window function, the window length and the readouts of every
// column, and the level of every column for frames holding a sine on a
// column and between columns, sines at the bottom, the middle and the top
// of the range, two tones a semitone apart, impulses at the frame's centre
// and away from it, noise, a sine in noise, and silence. And the promise
// the readouts across a column make: a tone anywhere reads its level.
#include "greatest.h"
#include "constant_q.h"
#include "fft_analyzer.h"
#include "test_signals.h"
#include "constant_q_golden.h"

#include <math.h>
#include <stdlib.h>

#define REL 1e-4          // of a level, on top of the truncation's floor, for float arithmetic
#define LEVEL_DB 0.8      // a tone anywhere reads within this of its level at its column

static constant_q_config_t golden_config(void) {
	return (constant_q_config_t){
		.sample_rate = CONSTANT_Q_GOLDEN_FS, .n = CONSTANT_Q_GOLDEN_N, .min_window = CONSTANT_Q_GOLDEN_MIN_WINDOW,
		.resolution_semitones = (float)CONSTANT_Q_GOLDEN_RESOLUTION, .window_function = CONSTANT_Q_GOLDEN_WINDOW_FUNCTION,
		.threshold_db = (float)CONSTANT_Q_GOLDEN_THRESHOLD_DB, .readouts_per_ridge = (float)CONSTANT_Q_GOLDEN_READOUTS_PER_RIDGE,
	};
}

// the columns' edges, spaced evenly in log frequency, as the oracle spaces them
static void golden_edges(double *edges) {
	for (unsigned c = 0; c <= CONSTANT_Q_GOLDEN_COLUMNS; c++)
		edges[c] = CONSTANT_Q_GOLDEN_F_LO * pow(CONSTANT_Q_GOLDEN_F_HI / CONSTANT_Q_GOLDEN_F_LO, (double)c / CONSTANT_Q_GOLDEN_COLUMNS);
}

// the frame's transform as the analyzer gives it under the rectangular window
static fft_analyzer_t *transformed(const float *x, uint32_t n) {
	fft_analyzer_t *fa = fft_analyzer_new(&(fft_analyzer_config_t){
		.fft_len = n, .window_function = WINDOW_FUNCTION_RECTANGULAR, .window_skew = 0.0f, .mode = FFT_ANALYZER_MIX,
	});
	fft_analyzer_prepare_to_stream(fa, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = CONSTANT_Q_GOLDEN_FS, .channels = 1,
		.buffer_frames = 256, .samples_per_display_frame = n,
	});
	fft_analyzer_enqueue_audio_data(fa, x, sizeof(float) * n);
	fft_analyzer_update(fa);
	return fa;
}

// Every window function's width at half amplitude agrees with the oracle's.
TEST bandwidths_match_golden(void) {
	for (int f = 0; f < WINDOW_FUNCTION_COUNT; f++)
		ASSERT_IN_RANGEm(window_function_names[f], constant_q_golden_bandwidths[f], window_function_bandwidth((window_function_t)f), 1e-9);
	PASS();
}

// Every column's window is as long, and its readouts as many, as the oracle says.
TEST windows_and_readouts_match_golden(void) {
	double edges[CONSTANT_Q_GOLDEN_COLUMNS + 1];
	golden_edges(edges);
	constant_q_config_t cfg = golden_config();
	constant_q_t *cq = constant_q_new(&cfg, edges, CONSTANT_Q_GOLDEN_COLUMNS);
	ASSERT(cq != NULL);
	ASSERT_EQ(CONSTANT_Q_GOLDEN_COLUMNS, constant_q_columns(cq));
	for (unsigned c = 0; c < CONSTANT_Q_GOLDEN_COLUMNS; c++) {
		ASSERT_EQ_FMT(constant_q_golden_windows[c], constant_q_window(cq, c), "%u");
		ASSERT_EQ_FMT(constant_q_golden_readouts[c], constant_q_readouts(cq, c), "%u");
	}
	ASSERT(constant_q_taps(cq) > CONSTANT_Q_GOLDEN_COLUMNS);
	constant_q_free(&cq);
	PASS();
}

// Each golden frame through the analyzer's unwindowed transform reads, at
// every column, the definition's level within the floor the truncation
// leaves and a small fraction of the level itself.
TEST readouts_match_golden(void) {
	enum { N = CONSTANT_Q_GOLDEN_N };
	double edges[CONSTANT_Q_GOLDEN_COLUMNS + 1];
	golden_edges(edges);
	constant_q_config_t cfg = golden_config();
	constant_q_t *cq = constant_q_new(&cfg, edges, CONSTANT_Q_GOLDEN_COLUMNS);
	ASSERT(cq != NULL);
	float *x = malloc(sizeof(float) * N);
	float db[CONSTANT_Q_GOLDEN_COLUMNS];
	double floor_amplitude = pow(10.0, CONSTANT_Q_GOLDEN_FLOOR_DB / 20.0);

	for (unsigned i = 0; i < CONSTANT_Q_GOLDEN_CASE_COUNT; i++) {
		const constant_q_golden_case_t *g = &constant_q_golden_cases[i];
		if (g->kind == SIGNAL_IMPULSE) {
			test_signal_impulse(x, N, g->index, g->amp);
		} else {
			test_signal_t s = { g->kind, g->freq, g->freq2, g->amp, g->phase, 0, g->seed, g->noise_amp };
			test_signal_generate(&s, x, N, CONSTANT_Q_GOLDEN_FS);
		}
		fft_analyzer_t *fa = transformed(x, N);
		constant_q_readout(cq, fft_analyzer_spectrum(fa, 0), db);
		fft_analyzer_free(&fa);

		for (unsigned c = 0; c < CONSTANT_Q_GOLDEN_COLUMNS; c++) {
			double expected = pow(10.0, g->db[c] / 20.0), got = pow(10.0, db[c] / 20.0);
			ASSERT_IN_RANGEm(g->label, expected, got, floor_amplitude + REL * expected);
		}
	}
	free(x);
	constant_q_free(&cq);
	PASS();
}

// A full-scale tone anywhere in the range, on a column's centre or not,
// reads within LEVEL_DB of 0 dB at the column it falls in.
TEST a_tone_anywhere_reads_its_level(void) {
	enum { N = CONSTANT_Q_GOLDEN_N, TONES = 24 };
	double edges[CONSTANT_Q_GOLDEN_COLUMNS + 1];
	golden_edges(edges);
	constant_q_config_t cfg = golden_config();
	constant_q_t *cq = constant_q_new(&cfg, edges, CONSTANT_Q_GOLDEN_COLUMNS);
	ASSERT(cq != NULL);
	float *x = malloc(sizeof(float) * N);
	float db[CONSTANT_Q_GOLDEN_COLUMNS];

	for (unsigned i = 0; i < TONES; i++) {
		double f = 25.0 * pow(18000.0 / 25.0, (i + 0.37) / TONES);
		test_signal_t s = { SIGNAL_SINE, f, 0, 1.0, 0.7, 0, 0, 0 };
		test_signal_generate(&s, x, N, CONSTANT_Q_GOLDEN_FS);
		fft_analyzer_t *fa = transformed(x, N);
		constant_q_readout(cq, fft_analyzer_spectrum(fa, 0), db);
		fft_analyzer_free(&fa);
		unsigned c = 0;
		while (c + 1 < CONSTANT_Q_GOLDEN_COLUMNS && edges[c + 1] <= f) c++;
		ASSERT_IN_RANGE(0.0, (double)db[c], LEVEL_DB);
	}
	free(x);
	constant_q_free(&cq);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(bandwidths_match_golden);
	RUN_TEST(windows_and_readouts_match_golden);
	RUN_TEST(readouts_match_golden);
	RUN_TEST(a_tone_anywhere_reads_its_level);
	GREATEST_MAIN_END();
}
