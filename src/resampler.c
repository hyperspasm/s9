#include "resampler.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MIN_N 9
#define MIN_K 10

typedef struct resampler {
	// configuration
	uint32_t N;
	uint32_t k;
	uint32_t n_sinc;    // sinc table size (N * k + 1)
	uint32_t center;    // center index of sinc table

	// pre-computed tables
	float *sinc;        // oversampled sinc table
	float *sinc_delta;  // for linear interpolation
	float *window;      // window weights per tap

	// circular input buffer
	float *input_buf;   // N samples
	uint32_t input_ix;  // next write position
	uint32_t input_count; // samples in buffer (for initial fill)

	// resampling state
	float position;     // fractional position (0.0 - 0.999...)
	float ratio;        // cached ratio
	float cutoff;       // anti-alias cutoff (0 < cutoff <= 1)
} resampler_t;

static void validate_params(uint32_t *N, uint32_t *k) {
	if (*N < MIN_N) *N = MIN_N;
	if ((*N & 1) == 0) (*N)++;  // force odd for symmetric kernel
	if (*k < MIN_K) *k = MIN_K;
}

static uint32_t sinc_table_size(uint32_t N, uint32_t k) {
	return (N * k) + 1;
}

// Blackman window - excellent sidelobe suppression (~58 dB)
static float blackman(uint32_t n, uint32_t N) {
	return 0.42f
	     - 0.5f  * cosf(2.0f * (float)M_PI * n / (N - 1))
	     + 0.08f * cosf(4.0f * (float)M_PI * n / (N - 1));
}

static void init_sinc_table(resampler_t *rs) {
	uint32_t n_sinc = rs->n_sinc;

	// center index: (N/2 + 1) * k
	rs->center = ((rs->N >> 1) + 1) * rs->k;

	// build oversampled sinc table
	for (uint32_t i = 0; i < n_sinc; i++) {
		float t = (float)i / (float)rs->k - (float)((rs->N >> 1) + 1);

		if (t == 0.0f) {
			rs->sinc[i] = 1.0f;
		} else {
			rs->sinc[i] = sinf(t * (float)M_PI) / (t * (float)M_PI);
		}
	}

	// pre-compute forward sinc deltas for linear interpolation
	for (uint32_t i = 0; i < n_sinc - 1; i++) {
		rs->sinc_delta[i] = rs->sinc[i + 1] - rs->sinc[i];
	}
	rs->sinc_delta[n_sinc - 1] = 0.0f;

	// build per-tap window weights with Blackman window
	for (uint32_t j = 0; j < rs->N; j++) {
		rs->window[j] = blackman(j, rs->N);
	}

	// normalize window weights for unity DC gain at fc=1.0, offset=0
	float sum = 0.0f;
	uint32_t idx = rs->k;
	for (uint32_t j = 0; j < rs->N; j++, idx += rs->k) {
		sum += rs->sinc[idx] * rs->window[j];
	}
	if (sum != 0.0f) {
		float scale = 1.0f / sum;
		for (uint32_t j = 0; j < rs->N; j++) {
			rs->window[j] *= scale;
		}
	}
}

// lookup sinc value at fractional table index with linear interpolation
static inline float sinc_lookup(const resampler_t *rs, float x) {
	uint32_t n_sinc = rs->n_sinc;

	// clamp x to valid range
	if (x < 0.0f) x = 0.0f;
	if (x > (float)(n_sinc - 1)) x = (float)(n_sinc - 1);

	// clamp integer index so i+1 is safe
	uint32_t i = (uint32_t)x;
	if (i >= n_sinc - 1) i = n_sinc - 2;

	float frac = x - (float)i;

	// linear interpolation
	return rs->sinc[i] + frac * rs->sinc_delta[i];
}

// compute interpolated output sample at fractional offset with anti-aliasing
static float sinc_interpolate(resampler_t *rs, float offset) {
	float fc = rs->cutoff;
	float center_f = (float)rs->center;

	// scale offset by oversampling factor
	float off_k = offset * (float)rs->k;

	// compute fractional part for phase interpolation
	float kf = off_k - floorf(off_k);

	// compute initial kernel integer index
	uint32_t ki = rs->k - (uint32_t)(off_k - kf);

	// perform convolution over circular buffer with anti-aliasing
	float accum = 0.0f;
	float coeff_sum = 0.0f;
	uint32_t tap = 0;
	uint32_t idx = ki;

	// part 1: input_ix to N-1
	for (uint32_t i = rs->input_ix; i < rs->N; i++, idx += rs->k, tap++) {
		float base = (float)idx - kf;
		float scaled = center_f + (base - center_f) * fc;
		float s = sinc_lookup(rs, scaled);
		float coeff = fc * s * rs->window[tap];
		accum += rs->input_buf[i] * coeff;
		coeff_sum += coeff;
	}

	// part 2: 0 to input_ix-1
	for (uint32_t i = 0; i < rs->input_ix; i++, idx += rs->k, tap++) {
		float base = (float)idx - kf;
		float scaled = center_f + (base - center_f) * fc;
		float s = sinc_lookup(rs, scaled);
		float coeff = fc * s * rs->window[tap];
		accum += rs->input_buf[i] * coeff;
		coeff_sum += coeff;
	}

	// normalize for unity gain
	if (coeff_sum != 0.0f) {
		accum /= coeff_sum;
	}

	return accum;
}

// push one sample into the circular input buffer
static void push_sample(resampler_t *rs, float sample) {
	rs->input_buf[rs->input_ix++] = sample;
	if (rs->input_ix >= rs->N) {
		rs->input_ix = 0;
	}
	if (rs->input_count < rs->N) {
		rs->input_count++;
	}
}

resampler_t *resampler_new(const resampler_config_t *config) {
	resampler_config_t cfg = config ? *config : default_resampler_config;

	uint32_t N = cfg.N;
	uint32_t k = cfg.k;
	validate_params(&N, &k);

	uint32_t n_sinc = sinc_table_size(N, k);

	resampler_t *rs = malloc(sizeof(resampler_t));
	if (!rs) return NULL;
	memset(rs, 0, sizeof(*rs));

	rs->N = N;
	rs->k = k;
	rs->n_sinc = n_sinc;

	// allocate tables
	rs->sinc = malloc(n_sinc * sizeof(float));
	rs->sinc_delta = malloc(n_sinc * sizeof(float));
	rs->window = malloc(N * sizeof(float));
	rs->input_buf = malloc(N * sizeof(float));

	if (!rs->sinc || !rs->sinc_delta || !rs->window || !rs->input_buf) {
		resampler_free(&rs);
		return NULL;
	}

	// initialize input buffer to zeros
	memset(rs->input_buf, 0, N * sizeof(float));

	// initialize sinc table and window
	init_sinc_table(rs);

	// initialize state
	rs->position = 0.0f;
	rs->ratio = 0.0f;  // will be set on first read
	rs->cutoff = 1.0f;

	return rs;
}

void resampler_free(resampler_t **rs) {
	if (rs && *rs) {
		free((*rs)->sinc);
		free((*rs)->sinc_delta);
		free((*rs)->window);
		free((*rs)->input_buf);
		free(*rs);
		*rs = NULL;
	}
}

uint32_t resampler_read(resampler_t *rs, float *out, uint32_t count, float ratio, resampler_source_fn source, void *context) {
	if (!rs || !out || !source || ratio <= 0.0f) return 0;

	// update ratio and cutoff if changed
	if (rs->ratio != ratio) {
		rs->ratio = ratio;
		// anti-alias cutoff: when downsampling (ratio < 1), reduce bandwidth
		rs->cutoff = (ratio < 1.0f) ? ratio : 1.0f;
	}

	// increment is how much we advance through input per output sample
	float increment = 1.0f / ratio;

	uint32_t written = 0;

	// temporary buffer for pulling input samples
	float sample;

	while (written < count) {
		// ensure we have enough samples in buffer for initial fill
		while (rs->input_count < rs->N / 2 + 1) {
			if (source(context, &sample, 1) == 0) {
				// source exhausted
				return written;
			}
			push_sample(rs, sample);
		}

		// compute interpolated output sample
		out[written++] = sinc_interpolate(rs, rs->position);

		// advance position
		rs->position += increment;

		// consume input samples as needed
		while (rs->position >= 1.0f) {
			rs->position -= 1.0f;

			if (source(context, &sample, 1) == 0) {
				// source exhausted
				return written;
			}
			push_sample(rs, sample);
		}
	}

	return written;
}
