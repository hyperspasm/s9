#ifndef fft_h
#define fft_h

#include <stdint.h>

// Transforms of real and complex frames, through PFFFT (extern/pffft). A
// transform is made for one length, which must be a fast length: a
// multiple of FFT_MIN_LENGTH with no prime factor above 5, the lengths
// PFFFT handles. The bins come out in the usual order as (re, im) pairs,
// unscaled, with the usual sign of the phase: a real frame of n samples
// gives n/2 + 1 bins, the first and the last with no imaginary part; a
// complex frame of n pairs gives n bins. The inverse of a real transform
// takes the same bins back to n samples, scaled by n. Input and output
// buffers may have any alignment.

#define FFT_MIN_LENGTH 32

typedef struct fft fft_t;

typedef enum { FFT_REAL, FFT_COMPLEX } fft_kind_t;

// the shortest fast length at or above n
uint32_t fft_fast_length(uint32_t n);

// a transform of a fast length; NULL for any other
fft_t *fft_new(uint32_t n, fft_kind_t kind);
void fft_free(fft_t **fft);

uint32_t fft_length(const fft_t *fft);

// n real samples to n/2 + 1 bins
void fft_forward_real(fft_t *fft, const float *in, float *bins);

// n complex samples to n bins
void fft_forward_complex(fft_t *fft, const float *in, float *bins);

// n/2 + 1 bins to n real samples, n times the frame that gave them
void fft_inverse_real(fft_t *fft, const float *bins, float *out);

#endif /* fft_h */
