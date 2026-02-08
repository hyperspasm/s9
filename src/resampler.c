#include "resampler.h"
#include "s9.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Resampler configuration (hardcoded for now)
#define RESAMPLER_N 17   // convolution length (filter taps)
#define RESAMPLER_K 64   // sinc table oversampling factor

typedef struct resampler {
	// Configuration
	uint32_t n_channels;        // number of channels

	// Sinc interpolation parameters
	uint32_t N;                 // convolution length (filter taps)
	uint32_t k;                 // sinc table oversampling factor
	uint32_t n_sinc;            // sinc table size (N * k + 1)
	uint32_t center;            // center index of sinc table

	// Pre-computed tables (shared across all channels)
	float *sinc;                // oversampled sinc table
	float *sinc_delta;          // for linear interpolation
	float *window;              // window weights per tap

	// Circular buffer (lock-free SPSC)
	float *input_buf;           // interleaved samples [L0,R0,L1,R1,...]
	uint32_t input_buf_frames;  // capacity in frames (power of 2)
	uint32_t buf_mask;          // input_buf_frames - 1, for fast indexing
	SDL_atomic_t write_ix;      // next write frame (audio thread)
	SDL_atomic_t read_ix;       // next read frame (render thread)

	// Resampling state (shared across channels)
	float position;             // fractional position (0.0 - 0.999...)
	float ratio;                // cached ratio
	float cutoff;               // anti-alias cutoff (0 < cutoff <= 1)
} resampler_t;

// Blackman window - excellent sidelobe suppression (~58 dB)
static float blackman(uint32_t n, uint32_t N) {
	return 0.42f
	     - 0.5f  * cosf(2.0f * (float)M_PI * n / (N - 1))
	     + 0.08f * cosf(4.0f * (float)M_PI * n / (N - 1));
}

static void init_sinc_table(resampler_t *s) {
	uint32_t n_sinc = s->n_sinc;

	// center index: (N/2 + 1) * k
	s->center = ((s->N >> 1) + 1) * s->k;

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
static inline float sinc_lookup(const resampler_t *s, float x) {
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

// compute interpolated output sample at fractional offset for a specific channel
// base_frame_ix: starting frame index in input_buf for the N-sample convolution window
// offset: fractional position within the frame (0.0 - 0.999...)
// channel: channel index to extract (0 to n_channels-1)
static float sinc_interpolate_channel(const resampler_t *s, uint32_t base_frame_ix, float offset, uint32_t channel) {
	S9_ASSERT(channel < s->n_channels);

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
		// calculate buffer position for this tap and channel
		uint32_t buf_frame = (base_frame_ix + tap) & s->buf_mask;
		uint32_t buf_idx = buf_frame * s->n_channels + channel;

		float base = (float)idx - kf;
		float scaled = center_f + (base - center_f) * fc;
		float sinc_val = sinc_lookup(s, scaled);
		float coeff = fc * sinc_val * s->window[tap];

		accum += s->input_buf[buf_idx] * coeff;
		coeff_sum += coeff;
	}

	// normalize for unity gain
	if (coeff_sum != 0.0f) {
		accum /= coeff_sum;
	}

	return accum;
}

// get number of frames available for reading
static inline uint32_t frames_available(uint32_t buf_mask, uint32_t write_ix, uint32_t read_ix) {
	return (write_ix - read_ix) & buf_mask;
}

// round up to next power of 2
static uint32_t next_power_of_2(uint32_t n) {
	if (n == 0) return 1;
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	return n + 1;
}

resampler_t *resampler_new(uint32_t channels, uint32_t buffer_size) {
	// validate parameters
	CHECK_PARAMETER(channels >= 1, return NULL);
	CHECK_PARAMETER(buffer_size >= RESAMPLER_N + 1,
		S9_LOG_ERROR("buffer_size %u too small, need at least %u", buffer_size, RESAMPLER_N + 1);
		return NULL);

	// round buffer size up to next power of 2
	uint32_t buffer_frames = next_power_of_2(buffer_size);

	// allocate struct
	resampler_t *s = malloc(sizeof(resampler_t));
	CHECK_ALLOC(s, return NULL);
	memset(s, 0, sizeof(*s));

	// set configuration
	s->n_channels = channels;
	s->N = RESAMPLER_N;
	s->k = RESAMPLER_K;
	s->n_sinc = s->N * s->k + 1;
	s->center = ((s->N >> 1) + 1) * s->k;

	// allocate sinc tables
	s->sinc = malloc(s->n_sinc * sizeof(float));
	CHECK_ALLOC(s->sinc, resampler_free(&s); return NULL);

	s->sinc_delta = malloc(s->n_sinc * sizeof(float));
	CHECK_ALLOC(s->sinc_delta, resampler_free(&s); return NULL);

	s->window = malloc(s->N * sizeof(float));
	CHECK_ALLOC(s->window, resampler_free(&s); return NULL);

	// initialize sinc table and window
	init_sinc_table(s);

	// allocate interleaved buffer
	s->input_buf_frames = buffer_frames;
	s->buf_mask = buffer_frames - 1;
	uint32_t buffer_samples = buffer_frames * channels;
	s->input_buf = malloc(buffer_samples * sizeof(float));
	CHECK_ALLOC(s->input_buf, resampler_free(&s); return NULL);
	memset(s->input_buf, 0, buffer_samples * sizeof(float));

	// initialize atomics
	SDL_AtomicSet(&s->write_ix, 0);
	SDL_AtomicSet(&s->read_ix, 0);

	// initialize resampling state
	s->position = 0.0f;
	s->ratio = 0.0f;
	s->cutoff = 1.0f;

	return s;
}

void resampler_free(resampler_t **sampler) {
	if (sampler && *sampler) {
		if ((*sampler)->sinc) free((*sampler)->sinc);
		if ((*sampler)->sinc_delta) free((*sampler)->sinc_delta);
		if ((*sampler)->window) free((*sampler)->window);
		if ((*sampler)->input_buf) free((*sampler)->input_buf);
		free(*sampler);
		*sampler = NULL;
	}
}

uint32_t resampler_enqueue(resampler_t *sampler, const float *samples, uint32_t frames, uint32_t n_channels) {
	if (!sampler || !samples || frames == 0) return 0;

	// validate channel count
	if (n_channels != sampler->n_channels) {
		S9_LOG_ERROR("resampler_enqueue: channel mismatch (expected %u, got %u)",
			sampler->n_channels, n_channels);
		return 0;
	}

	// snapshot read_ix (written by render thread)
	uint32_t read_ix_snap = (uint32_t)SDL_AtomicGet(&sampler->read_ix);

	// work with local write_ix (only audio thread writes this)
	uint32_t write_ix_local = (uint32_t)SDL_AtomicGet(&sampler->write_ix);

	// calculate available write space (in frames)
	// usable capacity is buf_mask (one frame kept empty)
	uint32_t avail = (read_ix_snap - write_ix_local - 1) & sampler->buf_mask;

	uint32_t to_write = frames;
	if (to_write > avail) {
		S9_LOG_ERROR("resampler buffer overflow: %u frames dropped", to_write - avail);
		to_write = avail;
	}

	// copy interleaved samples to buffer
	for (uint32_t i = 0; i < to_write; i++) {
		uint32_t buf_idx = write_ix_local * n_channels;
		for (uint32_t c = 0; c < n_channels; c++) {
			sampler->input_buf[buf_idx + c] = samples[i * n_channels + c];
		}
		write_ix_local = (write_ix_local + 1) & sampler->buf_mask;
	}

	// publish final write_ix with release semantics
	SDL_AtomicSet(&sampler->write_ix, (int)write_ix_local);

	return to_write;
}

uint32_t resampler_read(resampler_t *sampler, float *out, uint32_t max_frames, float rate, float gain) {
	if (!sampler || !out || max_frames == 0 || rate <= 0.0f) return 0;

	// update ratio and cutoff if changed
	if (sampler->ratio != rate) {
		sampler->ratio = rate;
		// anti-alias cutoff: when downsampling (rate < 1), reduce bandwidth
		sampler->cutoff = (rate < 1.0f) ? rate : 1.0f;
	}

	// increment is how much we advance through input per output sample
	float increment = 1.0f / rate;

	// snapshot write_ix (written by audio thread)
	uint32_t write_ix_snap = (uint32_t)SDL_AtomicGet(&sampler->write_ix);

	// work with local read_ix (only render thread writes this)
	uint32_t read_ix_local = (uint32_t)SDL_AtomicGet(&sampler->read_ix);

	// local copy of position for processing
	float position = sampler->position;

	uint32_t written = 0;
	uint32_t n_channels = sampler->n_channels;

	while (written < max_frames) {
		// check if we have enough frames for convolution + position advance
		uint32_t advance = (uint32_t)(position + increment);
		uint32_t avail = frames_available(sampler->buf_mask, write_ix_snap, read_ix_local);
		if (avail < sampler->N + advance) {
			break;  // not enough data
		}

		// resample each channel independently at the same fractional position
		for (uint32_t c = 0; c < n_channels; c++) {
			out[written * n_channels + c] =
				sinc_interpolate_channel(sampler, read_ix_local, position, c) * gain;
		}
		written++;

		// advance position
		position += increment;

		// consume input frames as needed
		while (position >= 1.0f) {
			position -= 1.0f;
			read_ix_local = (read_ix_local + 1) & sampler->buf_mask;
		}
	}

	// update state
	sampler->position = position;

	// publish final read_ix with release semantics
	SDL_AtomicSet(&sampler->read_ix, (int)read_ix_local);

	return written;
}
