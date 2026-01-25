#ifndef resampler_h
#define resampler_h

#include <stdint.h>

typedef struct resampler resampler_t;

typedef struct {
	uint32_t N;  // convolution length (filter taps), min 9
	uint32_t k;  // sinc table oversampling factor, min 10
} resampler_config_t;

static const resampler_config_t default_resampler_config = {
	.N = 17,
	.k = 64,
};

// callback type: fills `out` with up to `count` samples, returns actual count
typedef uint32_t (*resampler_source_fn)(void *context, float *out, uint32_t count);

// create resampler
resampler_t *resampler_new(const resampler_config_t *config);

// destroy resampler
void resampler_free(resampler_t **rs);

// read output samples, pulling from source as needed
// ratio is output_rate / input_rate (e.g., 16.0 for 16x upsampling)
// internally caches ratio and rebuilds filter when it changes
// returns number of samples actually written to `out`
uint32_t resampler_read(resampler_t *rs, float *out, uint32_t count, float ratio, resampler_source_fn source, void *context);

#endif /* resampler_h */
