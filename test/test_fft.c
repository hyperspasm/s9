// The FFT module: a real transform's inverse restores the frame, scaled by
// its length, and a sine lands in its bin.
#include "greatest.h"
#include "fft.h"

#include <math.h>
#include <stdlib.h>

TEST inverse_restores_the_frame(void) {
	uint32_t n = 4096;
	float *x = malloc(sizeof(float) * n), *y = malloc(sizeof(float) * n), *bins = malloc(sizeof(float) * (n + 2));
	uint32_t state = 12345;
	for (uint32_t i = 0; i < n; i++) {
		state = state * 1664525u + 1013904223u;
		x[i] = (float)((state >> 8) * (2.0 / 16777216.0) - 1.0);
	}
	fft_t *fft = fft_new(n, FFT_REAL);
	ASSERT(fft);
	fft_forward_real(fft, x, bins);
	fft_inverse_real(fft, bins, y);
	for (uint32_t i = 0; i < n; i++) ASSERT_IN_RANGE((double)x[i], (double)y[i] / n, 1e-5);
	fft_free(&fft);
	free(x); free(y); free(bins);
	PASS();
}

TEST a_sine_lands_in_its_bin(void) {
	uint32_t n = 1024, k = 37;
	float *x = malloc(sizeof(float) * n), *bins = malloc(sizeof(float) * (n + 2));
	for (uint32_t i = 0; i < n; i++) x[i] = (float)cos(2.0 * M_PI * k * i / n);
	fft_t *fft = fft_new(n, FFT_REAL);
	ASSERT(fft);
	fft_forward_real(fft, x, bins);
	ASSERT_IN_RANGE((double)n / 2.0, (double)bins[2 * k], 1e-2);   // amplitude 1 reads n/2 at its bin
	ASSERT_IN_RANGE(0.0, (double)bins[2 * (k + 1)], 1e-2);
	fft_free(&fft);
	free(x); free(bins);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(inverse_restores_the_frame);
	RUN_TEST(a_sine_lands_in_its_bin);
	GREATEST_MAIN_END();
}
