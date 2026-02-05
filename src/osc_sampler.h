/*
 * osc_sampler - Signal acquisition and triggering for oscilloscope display
 *
 * OVERVIEW
 * --------
 * This component encapsulates all signal processing for the oscilloscope:
 * buffering raw audio, resampling to pixel rate, trigger detection, and
 * outputting triggered sweep data. The oscilloscope itself becomes a consumer
 * of triggered, phase-aligned sweeps rather than managing these details.
 *
 * INTERNAL COMPONENTS
 * -------------------
 * - Custom circular buffer: Float-based, designed for resampler access pattern.
 *   Not using the generic ring_buffer because:
 *   - Need float-native storage (ring_buffer is byte-oriented)
 *   - Need to peek N samples for sinc convolution without consuming
 *   - Need to advance by 1 sample at a time as phase wraps
 *   - Can extract mono (channel 0) from interleaved input on enqueue
 *   Thread-safe via mutex for audio/render thread handoff.
 *
 * - Integrated resampler: Windowed-sinc interpolation implemented directly
 *   within osc_sampler (not wrapping the generic resampler). This allows:
 *   - Direct access to circular buffer for sinc convolution
 *   - Direct phase manipulation for 10x→1x rate switching
 *   - Avoiding callback overhead
 *
 * TRIGGER PRECISION STRATEGY
 * --------------------------
 * To minimize horizontal jitter on periodic waveforms, trigger detection runs
 * at 10x the pixel rate. When a threshold crossing is detected at this higher
 * resolution, the fractional phase offset is preserved so that subsequent
 * pixel-rate output is phase-aligned to the true trigger point. This gives
 * sub-pixel trigger precision (max jitter of 0.1 pixels) without the overhead
 * of resampling the entire sweep at 10x.
 *
 * Implementation approach:
 * 1. During pre-trigger/trigger states: resample at 10x pixel rate
 * 2. Detect threshold crossing with sub-pixel precision
 * 3. Once triggered, switch to 1x pixel rate for sweep output
 * 4. Resampler's internal phase state preserves alignment
 *
 * THREAD MODEL
 * ------------
 * - osc_sampler_enqueue(): Called from audio thread, writes to ring buffer
 * - osc_sampler_read_pixels(): Called from render thread, reads from ring buffer
 * - osc_sampler_sweep_complete(): Called from render thread
 * - osc_sampler_set_settings(): Called from render thread (or at init)
 * Ring buffer provides thread-safe handoff between audio and render threads.
 *
 * DATA FLOW
 * ---------
 *   Audio callback
 *         |
 *         v
 *   osc_sampler_enqueue() --> [Ring Buffer] --> osc_sampler_read_pixels()
 *                                                      |
 *                                                      v
 *                                               [Resampler at 10x]
 *                                                      |
 *                                                      v
 *                                               [Trigger Detection]
 *                                                      |
 *                                                      v (once triggered)
 *                                               [Resampler at 1x]
 *                                                      |
 *                                                      v
 *                                               Pixel-rate samples * gain
 *                                                      |
 *                                                      v
 *                                               Output buffer
 *
 * TRIGGER STATE MACHINE
 * ---------------------
 * - trig_state_wait: Waiting for holdoff period (in pixels) to elapse
 * - trig_state_pre_trigger: Waiting for signal to go "below" threshold
 *   (opposite side of the crossing we're looking for)
 * - trig_state_trigger: Waiting for signal to cross threshold in the
 *   configured direction (rising/falling)
 * - trig_state_sweep: Actively outputting sweep data
 *
 * Transitions:
 *   _sweep_complete() called --> trig_state_wait
 *   holdoff elapsed --> trig_state_pre_trigger (or sweep if auto mode)
 *   signal below threshold --> trig_state_trigger
 *   signal crosses threshold --> trig_state_sweep
 *
 * INTERFACE WITH OSCILLOSCOPE
 * ---------------------------
 * The oscilloscope calls read_pixels() to get sweep data. It tracks how many
 * pixels it has accumulated for the current sweep. When the sweep is complete
 * (enough pixels for display width), it calls sweep_complete() to reset the
 * trigger state machine.
 *
 * The sampler doesn't know the display width - it just produces samples at
 * the requested pixel rate until told a sweep is done. This keeps display
 * geometry concerns in the oscilloscope where they belong.
 */

#ifndef OSC_SAMPLER_H
#define OSC_SAMPLER_H

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
osc_sampler_t *osc_sampler_new(uint32_t sample_rate, uint32_t n_channels, uint32_t buffer_size);
void osc_sampler_free(osc_sampler_t **sampler);

// Configuration
void osc_sampler_set_settings(osc_sampler_t *sampler, const osc_sampler_settings_t *settings);

// Audio thread: enqueue raw interleaved samples
// Returns number of frames actually enqueued
uint32_t osc_sampler_enqueue(osc_sampler_t *sampler, const float *samples, uint32_t frames);

// Render thread: read resampled, triggered pixel data
// Returns number of pixels written (may be less than max_pixels if data unavailable)
uint32_t osc_sampler_read_pixels(osc_sampler_t *sampler, float *out, uint32_t max_pixels,
                                  float pixel_rate, float gain);

// Render thread: notify that a sweep has completed
// Allows sampler to transition trigger state appropriately
void osc_sampler_sweep_complete(osc_sampler_t *sampler);

#endif // OSC_SAMPLER_H
