#include "fft.h"
#include "s9.h"

#include "pffft.h"

#include <stdlib.h>
#include <string.h>

struct fft {
	uint32_t n;
	fft_kind_t kind;
	PFFFT_Setup *setup;
	float *in, *out, *work;   // aligned as PFFFT needs: n floats each, 2n for a complex transform
};

// whether a length has no prime factor above 5
static int smooth(uint32_t n) {
	for (uint32_t p = 2; p <= 5; p++)
		while (n % p == 0) n /= p;
	return n == 1;
}

uint32_t fft_fast_length(uint32_t n) {
	if (n < FFT_MIN_LENGTH) n = FFT_MIN_LENGTH;
	n = (n + FFT_MIN_LENGTH - 1) / FFT_MIN_LENGTH * FFT_MIN_LENGTH;
	while (!smooth(n)) n += FFT_MIN_LENGTH;
	return n;
}

fft_t *fft_new(uint32_t n, fft_kind_t kind) {
	CHECK_PARAMETER(n == fft_fast_length(n), return NULL);
	fft_t *fft = calloc(1, sizeof(fft_t));
	CHECK_ALLOC(fft, return NULL);
	fft->n = n;
	fft->kind = kind;
	size_t floats = kind == FFT_COMPLEX ? 2 * (size_t)n : n;
	fft->setup = pffft_new_setup((int)n, kind == FFT_COMPLEX ? PFFFT_COMPLEX : PFFFT_REAL);
	fft->in = pffft_aligned_malloc(sizeof(float) * floats);
	fft->out = pffft_aligned_malloc(sizeof(float) * floats);
	fft->work = pffft_aligned_malloc(sizeof(float) * floats);
	if (!fft->setup || !fft->in || !fft->out || !fft->work) {
		S9_LOG_ERROR("fft: could not set up a %u point transform", n);
		fft_free(&fft);
		return NULL;
	}
	return fft;
}

void fft_free(fft_t **fft) {
	if (fft && *fft) {
		if ((*fft)->setup) pffft_destroy_setup((*fft)->setup);
		pffft_aligned_free((*fft)->in);
		pffft_aligned_free((*fft)->out);
		pffft_aligned_free((*fft)->work);
		free(*fft);
		*fft = NULL;
	}
}

uint32_t fft_length(const fft_t *fft) { return fft->n; }

// PFFFT packs the two bins that are real, the first and the one at half
// the rate, into the first pair; the bins between follow in order
void fft_forward_real(fft_t *fft, const float *in, float *bins) {
	S9_ASSERT(fft->kind == FFT_REAL);
	uint32_t n = fft->n;
	memcpy(fft->in, in, sizeof(float) * n);
	pffft_transform_ordered(fft->setup, fft->in, fft->out, fft->work, PFFFT_FORWARD);
	bins[0] = fft->out[0];
	bins[1] = 0.0f;
	memcpy(bins + 2, fft->out + 2, sizeof(float) * (n - 2));
	bins[n] = fft->out[1];
	bins[n + 1] = 0.0f;
}

void fft_forward_complex(fft_t *fft, const float *in, float *bins) {
	S9_ASSERT(fft->kind == FFT_COMPLEX);
	memcpy(fft->in, in, sizeof(float) * 2 * fft->n);
	pffft_transform_ordered(fft->setup, fft->in, fft->out, fft->work, PFFFT_FORWARD);
	memcpy(bins, fft->out, sizeof(float) * 2 * fft->n);
}

void fft_inverse_real(fft_t *fft, const float *bins, float *out) {
	S9_ASSERT(fft->kind == FFT_REAL);
	uint32_t n = fft->n;
	fft->in[0] = bins[0];
	fft->in[1] = bins[n];
	memcpy(fft->in + 2, bins + 2, sizeof(float) * (n - 2));
	pffft_transform_ordered(fft->setup, fft->in, fft->out, fft->work, PFFFT_BACKWARD);
	memcpy(out, fft->out, sizeof(float) * n);
}
