#include "osc_sampler.h"
#include "s9.h"

#include <stdlib.h>
#include <string.h>

static const osc_sampler_settings_t default_settings = {
	.trigger_mode = osc_trig_mode_normal,
	.trig_slope = 1,
	.trig_thresh = 0.1f,
	.trig_holdoff = 0.00001f,  // 10µs
};

struct osc_sampler {
	uint32_t sample_rate;
	uint32_t n_channels;
	osc_sampler_settings_t settings;
};

osc_sampler_t *osc_sampler_new(uint32_t sample_rate, uint32_t n_channels) {
	osc_sampler_t *sampler = malloc(sizeof(osc_sampler_t));
	CHECK_ALLOC(sampler, return NULL);
	memset(sampler, 0, sizeof(*sampler));

	sampler->sample_rate = sample_rate;
	sampler->n_channels = n_channels;
	sampler->settings = default_settings;

	return sampler;
}

void osc_sampler_free(osc_sampler_t **sampler) {
	if (sampler && *sampler) {
		free(*sampler);
		*sampler = NULL;
	}
}

void osc_sampler_set_settings(osc_sampler_t *sampler, const osc_sampler_settings_t *settings) {
	if (!sampler || !settings) return;
	sampler->settings = *settings;
}

uint32_t osc_sampler_enqueue(osc_sampler_t *sampler, const float *samples, uint32_t frames) {
	if (!sampler || !samples) return 0;
	// TODO: implement circular buffer write
	return 0;
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
