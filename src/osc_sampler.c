#include "osc_sampler.h"
#include "s9.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL_mutex.h>

static const osc_sampler_settings_t default_settings = {
	.trigger_mode = osc_trig_mode_normal,
	.trig_slope = 1,
	.trig_thresh = 0.1f,
	.trig_holdoff = 0.00001f,  // 10µs
};

struct osc_sampler {
	uint32_t sample_rate;
	osc_sampler_settings_t settings;

	// circular buffer
	// head == tail means empty, (head+1) % size == tail means full
	// one slot is always kept empty to distinguish full from empty
	// usable capacity is buffer_size - 1
	float *buffer;
	uint32_t buffer_size;
	uint32_t head;  // next write position (advanced by audio thread)
	uint32_t tail;  // next read position (advanced by render thread)
	SDL_mutex *mutex;
};

osc_sampler_t *osc_sampler_new(uint32_t sample_rate, uint32_t buffer_size) {
	osc_sampler_t *sampler = malloc(sizeof(osc_sampler_t));
	CHECK_ALLOC(sampler, return NULL);
	memset(sampler, 0, sizeof(*sampler));

	sampler->sample_rate = sample_rate;
	sampler->settings = default_settings;

	sampler->buffer = malloc(buffer_size * sizeof(float));
	CHECK_ALLOC(sampler->buffer,
		free(sampler);
		return NULL);
	sampler->buffer_size = buffer_size;
	sampler->head = 0;
	sampler->tail = 0;

	sampler->mutex = SDL_CreateMutex();
	CHECK_ERROR(sampler->mutex != NULL,
		free(sampler->buffer);
		free(sampler);
		return NULL,
		"failed to create mutex");

	return sampler;
}

void osc_sampler_free(osc_sampler_t **sampler) {
	if (sampler && *sampler) {
		if ((*sampler)->mutex) {
			SDL_DestroyMutex((*sampler)->mutex);
		}
		free((*sampler)->buffer);
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
	// usable capacity is buffer_size - 1 (one slot kept empty)
	// used = (head - tail + buffer_size) % buffer_size
	// avail = (buffer_size - 1) - used
	uint32_t avail;
	if (sampler->head >= sampler->tail) {
		avail = sampler->buffer_size - 1 - (sampler->head - sampler->tail);
	} else {
		avail = sampler->tail - sampler->head - 1;
	}

	uint32_t to_write = frames;
	if (to_write > avail) {
		S9_LOG_ERROR("osc_sampler buffer overflow: %u frames dropped", to_write - avail);
		to_write = avail;
	}

	// extract channel 0 from interleaved input and write to circular buffer
	for (uint32_t i = 0; i < to_write; i++) {
		sampler->buffer[sampler->head] = samples[i * n_channels];
		sampler->head = (sampler->head + 1) % sampler->buffer_size;
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
