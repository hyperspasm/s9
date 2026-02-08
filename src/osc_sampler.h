#ifndef osc_sampler_h
#define osc_sampler_h

#include <stdint.h>

typedef enum {
	osc_trig_mode_normal,
	osc_trig_mode_auto,
} osc_trigger_mode_t;

typedef struct {
	osc_trigger_mode_t trigger_mode;
	int trig_slope;           // 1 = rising, -1 = falling
	float trig_thresh;        // -1.0 to 1.0
	float trig_holdoff;       // seconds
} osc_sampler_settings_t;

typedef struct osc_sampler osc_sampler_t;

// Create/destroy
osc_sampler_t *osc_sampler_new(uint32_t sample_rate, uint32_t buffer_size);
void osc_sampler_free(osc_sampler_t **sampler);

// Configuration
void osc_sampler_set_settings(osc_sampler_t *sampler, const osc_sampler_settings_t *settings);

// Audio thread: enqueue raw interleaved samples
// Returns number of frames actually enqueued
uint32_t osc_sampler_enqueue(osc_sampler_t *sampler, const float *samples, uint32_t frames, uint32_t n_channels);

// Render thread: read resampled, triggered pixel data
// Returns number of pixels written (may be less than max_pixels if data unavailable)
// If triggered is non-NULL, set to 1 if in sweep state on return, 0 otherwise
uint32_t osc_sampler_read_pixels(osc_sampler_t *sampler, float *out, uint32_t max_pixels,
                                  float pixel_rate, float gain, int *triggered);

// Render thread: notify that a sweep has completed
// Allows sampler to transition trigger state appropriately
void osc_sampler_sweep_complete(osc_sampler_t *sampler);

#endif /* osc_sampler_h */
