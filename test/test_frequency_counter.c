// The frequency counter, held to what it promises: a sine reads true to a
// thousandth of a cent over a second, whatever its period in samples and at
// any sample rate, and still to a hundredth with noise on it; a square reads
// true; noise and silence read nothing; and the reading ends soon after the
// signal does.
#include "greatest.h"
#include "frequency_counter.h"
#include "test_signals.h"

#include <math.h>
#include <stdlib.h>

#define HOP 480

static double cents_from(double f, double ref) { return 1200.0 * log2(f / ref); }

// feed a signal hop by hop and return the last reading's validity and frequency
static bool read_after(frequency_counter_t *fc, const float *x, uint32_t n, float *frequency) {
	for (uint32_t at = 0; at + HOP <= n; at += HOP) frequency_counter_feed(fc, x + at, HOP);
	return frequency_counter_read(fc, frequency);
}

TEST a_sine_reads_true_to_a_thousandth_of_a_cent(void) {
	const double rates[] = { 48000.0, 44100.0 };
	const double freqs[] = { 98.0, 98.37, 440.0, 1234.5 };
	for (unsigned r = 0; r < 2; r++) {
		for (unsigned k = 0; k < 4; k++) {
			uint32_t fs = (uint32_t)rates[r], n = 2 * fs;
			float *x = calloc(n, sizeof(float));
			test_signal_t s = { SIGNAL_SINE, freqs[k], 0.0, 0.25, 0.7, 0, 1, 0.0 };
			test_signal_generate(&s, x, n, rates[r]);
			frequency_counter_t *fc = frequency_counter_new(NULL, fs);
			ASSERT(fc);
			float f;
			ASSERTm("valid", read_after(fc, x, n, &f));
			ASSERT_IN_RANGE(0.0, cents_from(f, freqs[k]), 0.001);
			frequency_counter_free(&fc);
			free(x);
		}
	}
	PASS();
}

TEST noise_on_a_sine_costs_little(void) {
	uint32_t fs = 48000, n = 2 * fs;
	float *x = calloc(n, sizeof(float));
	test_signal_t s = { SIGNAL_SINE, 98.0, 0.0, 0.25, 0.0, 0, 1, 0.25e-3 };   // noise 60 dB down
	test_signal_generate(&s, x, n, fs);
	frequency_counter_t *fc = frequency_counter_new(NULL, fs);
	float f;
	ASSERT(read_after(fc, x, n, &f));
	ASSERT_IN_RANGE(0.0, cents_from(f, 98.0), 0.01);
	frequency_counter_free(&fc);
	free(x);
	PASS();
}

TEST a_square_reads_true(void) {
	uint32_t fs = 48000, n = 2 * fs;
	float *x = calloc(n, sizeof(float));
	test_signal_t s = { SIGNAL_SQUARE, 220.0, 0.0, 0.5, 0.0, 30, 1, 0.0 };
	test_signal_generate(&s, x, n, fs);
	frequency_counter_t *fc = frequency_counter_new(NULL, fs);
	float f;
	ASSERT(read_after(fc, x, n, &f));
	ASSERT_IN_RANGE(0.0, cents_from(f, 220.0), 0.01);
	frequency_counter_free(&fc);
	free(x);
	PASS();
}

TEST noise_and_silence_read_nothing(void) {
	uint32_t fs = 48000, n = 2 * fs;
	float *x = calloc(n, sizeof(float));
	test_signal_t s = { SIGNAL_NOISE, 0.0, 0.0, 0.3, 0.0, 0, 7, 0.0 };
	test_signal_generate(&s, x, n, fs);
	frequency_counter_t *fc = frequency_counter_new(NULL, fs);
	float f;
	ASSERT_FALSE(read_after(fc, x, n, &f));
	ASSERT_EQ_FMT(0.0f, f, "%g");
	frequency_counter_reset(fc);
	memset(x, 0, sizeof(float) * n);
	ASSERT_FALSE(read_after(fc, x, n, &f));
	frequency_counter_free(&fc);
	free(x);
	PASS();
}

TEST the_reading_ends_soon_after_the_signal(void) {
	uint32_t fs = 48000, n = 2 * fs;
	float *x = calloc(n, sizeof(float));
	test_signal_t s = { SIGNAL_SINE, 98.0, 0.0, 0.25, 0.0, 0, 1, 0.0 };
	test_signal_generate(&s, x, fs, fs);   // one second of note, one of silence
	frequency_counter_t *fc = frequency_counter_new(NULL, fs);
	float f;
	double ended = -1.0;
	for (uint32_t at = 0; at + HOP <= n; at += HOP) {
		frequency_counter_feed(fc, x + at, HOP);
		bool valid = frequency_counter_read(fc, &f);
		if (at + HOP == fs) ASSERT(valid);
		if (at + HOP > fs && !valid && ended < 0.0) ended = (double)(at + HOP) / fs;
	}
	ASSERT(ended > 1.0);
	ASSERT(ended < 1.05);   // within three periods of 98 Hz, plus a hop
	frequency_counter_free(&fc);
	free(x);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(a_sine_reads_true_to_a_thousandth_of_a_cent);
	RUN_TEST(noise_on_a_sine_costs_little);
	RUN_TEST(a_square_reads_true);
	RUN_TEST(noise_and_silence_read_nothing);
	RUN_TEST(the_reading_ends_soon_after_the_signal);
	GREATEST_MAIN_END();
}
