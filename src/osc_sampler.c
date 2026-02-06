#include "osc_sampler.h"
#include "s9.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL_atomic.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const osc_sampler_settings_t default_settings = {
	.trigger_mode = osc_trig_mode_normal,
	.trig_slope = 1,
	.trig_thresh = 0.1f,
	.trig_holdoff = 0.00001f,  // 10µs
};

struct osc_sampler {
	uint32_t sample_rate;
	osc_sampler_settings_t settings;

	// input buffer (circular, lock-free SPSC)
	// write_ix == read_ix means empty, (write_ix+1) % size == read_ix means full
	// one slot is always kept empty to distinguish full from empty
	// usable capacity is input_buf_size - 1
	float *input_buf;
	uint32_t input_buf_size;  // always a power of two
	uint32_t buf_mask;        // input_buf_size - 1, for fast modular indexing
	SDL_atomic_t write_ix;    // next write position (advanced by audio thread)
	SDL_atomic_t read_ix;     // next read position (advanced by render thread)

	// resampler configuration
	uint32_t N;         // convolution length (filter taps)
	uint32_t k;         // sinc table oversampling factor

	// resampler pre-computed tables
	uint32_t n_sinc;    // sinc table size (N * k + 1)
	uint32_t center;    // center index of sinc table
	float *sinc;        // oversampled sinc table
	float *sinc_delta;  // for linear interpolation
	float *window;      // window weights per tap

	// resampler state
	float position;     // fractional position (0.0 - 0.999...)
	float ratio;        // cached ratio
	float cutoff;       // anti-alias cutoff (0 < cutoff <= 1)
};

// Blackman window - excellent sidelobe suppression (~58 dB)
static float blackman(uint32_t n, uint32_t N) {
	return 0.42f
	     - 0.5f  * cosf(2.0f * (float)M_PI * n / (N - 1))
	     + 0.08f * cosf(4.0f * (float)M_PI * n / (N - 1));
}

static void init_sinc_table(osc_sampler_t *s) {
	uint32_t n_sinc = s->n_sinc;

	// build oversampled sinc table
	for (uint32_t i = 0; i < n_sinc; i++) {
		float t = (float)i / (float)s->k - (float)((s->N >> 1) + 1);

		if (t == 0.0f) {
			s->sinc[i] = 1.0f;
		} else {
			s->sinc[i] = sinf(t * (float)M_PI) / (t * (float)M_PI);
		}
	}

	// pre-compute forward sinc deltas for linear interpolation
	for (uint32_t i = 0; i < n_sinc - 1; i++) {
		s->sinc_delta[i] = s->sinc[i + 1] - s->sinc[i];
	}
	s->sinc_delta[n_sinc - 1] = 0.0f;

	// build per-tap window weights with Blackman window
	for (uint32_t j = 0; j < s->N; j++) {
		s->window[j] = blackman(j, s->N);
	}

	// normalize window weights for unity DC gain at fc=1.0, offset=0
	float sum = 0.0f;
	uint32_t idx = s->k;
	for (uint32_t j = 0; j < s->N; j++, idx += s->k) {
		sum += s->sinc[idx] * s->window[j];
	}
	if (sum != 0.0f) {
		float scale = 1.0f / sum;
		for (uint32_t j = 0; j < s->N; j++) {
			s->window[j] *= scale;
		}
	}
}

// lookup sinc value at fractional table index with linear interpolation
static inline float sinc_lookup(const osc_sampler_t *s, float x) {
	uint32_t n_sinc = s->n_sinc;

	// clamp x to valid range
	if (x < 0.0f) x = 0.0f;
	if (x > (float)(n_sinc - 1)) x = (float)(n_sinc - 1);

	// clamp integer index so i+1 is safe
	uint32_t i = (uint32_t)x;
	if (i >= n_sinc - 1) i = n_sinc - 2;

	float frac = x - (float)i;

	// linear interpolation using pre-computed deltas
	return s->sinc[i] + frac * s->sinc_delta[i];
}

// compute interpolated output sample at fractional offset with anti-aliasing
// base_ix is the starting index in input_buf for the N-sample convolution window
static float sinc_interpolate(const osc_sampler_t *s, uint32_t base_ix, float offset) {
	float fc = s->cutoff;
	float center_f = (float)s->center;

	// scale offset by oversampling factor
	float off_k = offset * (float)s->k;

	// compute fractional part for phase interpolation
	float kf = off_k - floorf(off_k);

	// compute initial kernel integer index
	uint32_t ki = s->k - (uint32_t)(off_k - kf);

	// perform convolution over input buffer with anti-aliasing
	float accum = 0.0f;
	float coeff_sum = 0.0f;
	uint32_t idx = ki;

	for (uint32_t tap = 0; tap < s->N; tap++, idx += s->k) {
		uint32_t buf_ix = (base_ix + tap) & s->buf_mask;

		float base = (float)idx - kf;
		float scaled = center_f + (base - center_f) * fc;
		float sinc_val = sinc_lookup(s, scaled);
		float coeff = fc * sinc_val * s->window[tap];

		accum += s->input_buf[buf_ix] * coeff;
		coeff_sum += coeff;
	}

	// normalize for unity gain
	if (coeff_sum != 0.0f) {
		accum /= coeff_sum;
	}

	return accum;
}

// get number of samples available for reading given snapshotted indices
static inline uint32_t samples_available(uint32_t buf_mask, uint32_t write_ix, uint32_t read_ix) {
	return (write_ix - read_ix) & buf_mask;
}

// resample from input buffer to output buffer
// returns number of output samples written
static uint32_t resample_read(osc_sampler_t *s, float *out, uint32_t count, float ratio) {
	if (ratio <= 0.0f) return 0;

	// update ratio and cutoff if changed
	if (s->ratio != ratio) {
		s->ratio = ratio;
		// anti-alias cutoff: when downsampling (ratio < 1), reduce bandwidth
		s->cutoff = (ratio < 1.0f) ? ratio : 1.0f;
	}

	// increment is how much we advance through input per output sample
	float increment = 1.0f / ratio;

	// minimum samples needed: N for convolution
	uint32_t min_samples = s->N;

	// snapshot write_ix once (written by audio thread)
	uint32_t write_ix_snap = (uint32_t)SDL_AtomicGet(&s->write_ix);

	// work with local read_ix (only render thread writes this)
	uint32_t read_ix_local = (uint32_t)SDL_AtomicGet(&s->read_ix);

	uint32_t written = 0;

	while (written < count) {
		// check if we have enough samples
		uint32_t avail = samples_available(s->buf_mask, write_ix_snap, read_ix_local);
		if (avail < min_samples) {
			break;  // not enough data
		}

		// compute interpolated output sample
		out[written++] = sinc_interpolate(s, read_ix_local, s->position);

		// advance position
		s->position += increment;

		// consume input samples as needed
		while (s->position >= 1.0f) {
			s->position -= 1.0f;
			read_ix_local = (read_ix_local + 1) & s->buf_mask;
		}
	}

	// publish final read_ix with release semantics
	SDL_AtomicSet(&s->read_ix, (int)read_ix_local);

	return written;
}

osc_sampler_t *osc_sampler_new(uint32_t sample_rate, uint32_t buffer_size) {
	osc_sampler_t *sampler = malloc(sizeof(osc_sampler_t));
	CHECK_ALLOC(sampler, return NULL);
	memset(sampler, 0, sizeof(*sampler));

	sampler->sample_rate = sample_rate;
	sampler->settings = default_settings;

	// resampler configuration (hardcoded for now)
	sampler->N = 17;
	sampler->k = 64;
	sampler->n_sinc = sampler->N * sampler->k + 1;
	sampler->center = ((sampler->N >> 1) + 1) * sampler->k;

	// allocate resampler tables
	sampler->sinc = malloc(sampler->n_sinc * sizeof(float));
	CHECK_ALLOC(sampler->sinc, osc_sampler_free(&sampler); return NULL);

	sampler->sinc_delta = malloc(sampler->n_sinc * sizeof(float));
	CHECK_ALLOC(sampler->sinc_delta, osc_sampler_free(&sampler); return NULL);

	sampler->window = malloc(sampler->N * sizeof(float));
	CHECK_ALLOC(sampler->window, osc_sampler_free(&sampler); return NULL);

	init_sinc_table(sampler);

	// initialize resampler state
	sampler->position = 0.0f;
	sampler->ratio = 0.0f;
	sampler->cutoff = 1.0f;

	// input buffer needs at least N samples for convolution + 1 for empty slot
	uint32_t min_buffer_size = sampler->N + 1;
	CHECK_ERROR(buffer_size >= min_buffer_size,
		osc_sampler_free(&sampler); return NULL,
		"buffer_size %u too small, need at least %u", buffer_size, min_buffer_size);

	// round up to next power of two for fast modular indexing
	uint32_t pow2 = 1;
	while (pow2 < buffer_size) pow2 <<= 1;

	sampler->input_buf = malloc(pow2 * sizeof(float));
	CHECK_ALLOC(sampler->input_buf, osc_sampler_free(&sampler); return NULL);
	memset(sampler->input_buf, 0, pow2 * sizeof(float));
	sampler->input_buf_size = pow2;
	sampler->buf_mask = pow2 - 1;

	// initialize atomic indices
	SDL_AtomicSet(&sampler->write_ix, 0);
	SDL_AtomicSet(&sampler->read_ix, 0);

	return sampler;
}

void osc_sampler_free(osc_sampler_t **sampler) {
	if (sampler && *sampler) {
		if ((*sampler)->sinc) free((*sampler)->sinc);
		if ((*sampler)->sinc_delta) free((*sampler)->sinc_delta);
		if ((*sampler)->window) free((*sampler)->window);
		if ((*sampler)->input_buf) free((*sampler)->input_buf);
		free(*sampler);
		*sampler = NULL;
	}
}

void osc_sampler_set_settings(osc_sampler_t *sampler, const osc_sampler_settings_t *settings) {
	if (!sampler || !settings) return;
	sampler->settings = *settings;
}

uint32_t osc_sampler_enqueue(osc_sampler_t *sampler, const float *samples, uint32_t frames, uint32_t n_channels) {
	if (!sampler || !samples || frames == 0) return 0;

	// snapshot read_ix (written by render thread)
	uint32_t read_ix_snap = (uint32_t)SDL_AtomicGet(&sampler->read_ix);

	// work with local write_ix (only audio thread writes this)
	uint32_t write_ix_local = (uint32_t)SDL_AtomicGet(&sampler->write_ix);

	// calculate available write space
	// usable capacity is buf_mask (input_buf_size - 1, one slot kept empty)
	uint32_t avail = (read_ix_snap - write_ix_local - 1) & sampler->buf_mask;

	uint32_t to_write = frames;
	if (to_write > avail) {
		S9_LOG_ERROR("osc_sampler buffer overflow: %u frames dropped", to_write - avail);
		to_write = avail;
	}

	// extract channel 0 from interleaved input and write to input buffer
	for (uint32_t i = 0; i < to_write; i++) {
		sampler->input_buf[write_ix_local] = samples[i * n_channels];
		write_ix_local = (write_ix_local + 1) & sampler->buf_mask;
	}

	// publish final write_ix with release semantics
	SDL_AtomicSet(&sampler->write_ix, (int)write_ix_local);

	return to_write;
}

uint32_t osc_sampler_read_pixels(osc_sampler_t *sampler, float *out, uint32_t max_pixels,
                                  float pixel_rate, float gain) {
	if (!sampler || !out || pixel_rate <= 0.0f) return 0;
	// TODO: implement resampling, triggering, output
	return 0;
}

void osc_sampler_sweep_complete(osc_sampler_t *sampler) {
	if (!sampler) return;
	// TODO: transition trigger state to wait
}
