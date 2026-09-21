#ifndef frequency_counter_h
#define frequency_counter_h

#include <stdbool.h>
#include <stdint.h>

// A frequency counter over a mono stream, for waveforms with one positive-going
// zero crossing per cycle: a sine, a saw, a square. Each crossing is timed to a
// fraction of a sample by linear interpolation between the two samples that
// straddle zero, and confirmed only when the signal goes on to clear a
// hysteresis band about zero, so noise and hum on the crossing do not count
// twice. The reading is the reciprocal count over a window ending at the
// newest sample: the cycles between the first and the last crossing in it,
// divided by the time between them, so only the two end crossings' timing
// errors remain. It is valid while the window holds a few crossings at a
// regular spacing, the last of them recent, and the signal clears the gate.
// A waveform crossing more than once per cycle reads wrong by design.

typedef struct frequency_counter frequency_counter_t;

typedef struct {
	float window_s;       // the crossings within this long before the newest sample count
	float hysteresis;     // a crossing is confirmed once the signal has gone from below minus this fraction
	                      // of its peak to above plus it
	float gate_dbfs;      // no reading while the signal's peak is below this
	float regularity;     // no reading while any period in the window differs from their mean by more
	                      // than this fraction
} frequency_counter_config_t;

static const frequency_counter_config_t default_frequency_counter_config = {
	.window_s = 1.0f,
	.hysteresis = 0.1f,
	.gate_dbfs = -60.0f,
	.regularity = 0.1f,
};

// a counter for a sample rate; NULL takes the default configuration
frequency_counter_t *frequency_counter_new(const frequency_counter_config_t *config, uint32_t sample_rate);
void frequency_counter_free(frequency_counter_t **fc);

// forget everything heard, as at a stream's start
void frequency_counter_reset(frequency_counter_t *fc);

// feed mono samples
void frequency_counter_feed(frequency_counter_t *fc, const float *samples, uint32_t count);

// the reading over the window ending at the newest sample: whether it is valid,
// and the frequency in Hz, 0 when it is not
bool frequency_counter_read(const frequency_counter_t *fc, float *frequency);

#endif /* frequency_counter_h */
