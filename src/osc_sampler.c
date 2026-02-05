#include "osc_sampler.h"
#include "s9.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL_mutex.h>

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

	// input buffer (circular)
	// write_ix == read_ix means empty, (write_ix+1) % size == read_ix means full
	// one slot is always kept empty to distinguish full from empty
	// usable capacity is input_buf_size - 1
	float *input_buf;
	uint32_t input_buf_size;
	uint32_t write_ix;  // next write position (advanced by audio thread)
	uint32_t read_ix;   // next read position (advanced by render thread)
	SDL_mutex *mutex;

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

osc_sampler_t *osc_sampler_new(uint32_t sample_rate, uint32_t buffer_size) {
	osc_sampler_t *sampler = malloc(sizeof(osc_sampler_t));
	CHECK_ALLOC(sampler, return NULL);
	memset(sampler, 0, sizeof(*sampler));

	sampler->sample_rate = sample_rate;
	sampler->settings = default_settings;

	sampler->input_buf = malloc(buffer_size * sizeof(float));
	CHECK_ALLOC(sampler->input_buf,
		free(sampler);
		return NULL);
	sampler->input_buf_size = buffer_size;
	sampler->write_ix = 0;
	sampler->read_ix = 0;

	sampler->mutex = SDL_CreateMutex();
	CHECK_ERROR(sampler->mutex != NULL,
		free(sampler->input_buf);
		free(sampler);
		return NULL,
		"failed to create mutex");

	// resampler configuration (hardcoded for now)
	sampler->N = 17;
	sampler->k = 64;
	sampler->n_sinc = sampler->N * sampler->k + 1;
	sampler->center = ((sampler->N >> 1) + 1) * sampler->k;

	// allocate resampler tables
	sampler->sinc = malloc(sampler->n_sinc * sizeof(float));
	sampler->sinc_delta = malloc(sampler->n_sinc * sizeof(float));
	sampler->window = malloc(sampler->N * sizeof(float));
	CHECK_ALLOC(sampler->sinc && sampler->sinc_delta && sampler->window,
		free(sampler->sinc);
		free(sampler->sinc_delta);
		free(sampler->window);
		SDL_DestroyMutex(sampler->mutex);
		free(sampler->input_buf);
		free(sampler);
		return NULL);

	init_sinc_table(sampler);

	// initialize resampler state
	sampler->position = 0.0f;
	sampler->ratio = 0.0f;
	sampler->cutoff = 1.0f;

	return sampler;
}

void osc_sampler_free(osc_sampler_t **sampler) {
	if (sampler && *sampler) {
		free((*sampler)->sinc);
		free((*sampler)->sinc_delta);
		free((*sampler)->window);
		if ((*sampler)->mutex) {
			SDL_DestroyMutex((*sampler)->mutex);
		}
		free((*sampler)->input_buf);
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

	SDL_LockMutex(sampler->mutex);

	// calculate available write space
	// usable capacity is input_buf_size - 1 (one slot kept empty)
	// used = (write_ix - read_ix + input_buf_size) % input_buf_size
	// avail = (input_buf_size - 1) - used
	uint32_t avail;
	if (sampler->write_ix >= sampler->read_ix) {
		avail = sampler->input_buf_size - 1 - (sampler->write_ix - sampler->read_ix);
	} else {
		avail = sampler->read_ix - sampler->write_ix - 1;
	}

	uint32_t to_write = frames;
	if (to_write > avail) {
		S9_LOG_ERROR("osc_sampler buffer overflow: %u frames dropped", to_write - avail);
		to_write = avail;
	}

	// extract channel 0 from interleaved input and write to input buffer
	for (uint32_t i = 0; i < to_write; i++) {
		sampler->input_buf[sampler->write_ix] = samples[i * n_channels];
		sampler->write_ix = (sampler->write_ix + 1) % sampler->input_buf_size;
	}

	SDL_UnlockMutex(sampler->mutex);

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
