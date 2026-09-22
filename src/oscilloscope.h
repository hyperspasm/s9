#ifndef oscilloscope_h
#define oscilloscope_h

#include "s9_dsp.h"
#include "s9_colors.h"

#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct oscilloscope oscilloscope_t;

// divisions across the screen: a sweep is this many times time_per_div
#define OSCILLOSCOPE_DIVISIONS 10

typedef struct {
	SDL_Color trace_color;
	float trace_width;
	uint8_t persistence;  // 0-255, higher = longer trace persistence
} oscilloscope_config_t;

static const oscilloscope_config_t default_oscilloscope_config = {
	.trace_color = S9_ELECTRIC,
	.trace_width = 2.f,
	.persistence = 200,
};

// create oscilloscope
oscilloscope_t *oscilloscope_new(const oscilloscope_config_t *config);

// destroy oscilloscope
void oscilloscope_free(oscilloscope_t **osc);

// render oscilloscope
void oscilloscope_render(oscilloscope_t *osc, SDL_Renderer *renderer);

// replace the configuration; NULL takes the default
void oscilloscope_set_config(oscilloscope_t *osc, const oscilloscope_config_t *config);

// settings
void oscilloscope_set_time_per_div(oscilloscope_t *osc, float seconds);
float oscilloscope_get_time_per_div(const oscilloscope_t *osc);
void oscilloscope_set_amp_per_div(oscilloscope_t *osc, float amplitude);
float oscilloscope_get_amp_per_div(const oscilloscope_t *osc);
void oscilloscope_set_trig_auto(oscilloscope_t *osc, bool auto_mode);
bool oscilloscope_get_trig_auto(const oscilloscope_t *osc);
void oscilloscope_set_trig_slope(oscilloscope_t *osc, int slope);
int oscilloscope_get_trig_slope(const oscilloscope_t *osc);
void oscilloscope_set_trig_thresh(oscilloscope_t *osc, float thresh);
float oscilloscope_get_trig_thresh(const oscilloscope_t *osc);
void oscilloscope_set_trig_holdoff(oscilloscope_t *osc, float seconds);
float oscilloscope_get_trig_holdoff(const oscilloscope_t *osc);
// Lock the trigger to the phase of the component at a frequency: each sweep
// starts where that component crosses zero in the slope's direction,
// whatever the rest of the signal is doing. 0 returns the trigger to the
// signal's own threshold crossings. The phase is measured over the given
// number of the component's cycles.
void oscilloscope_set_trig_lock(oscilloscope_t *osc, float frequency_hz);
float oscilloscope_get_trig_lock(const oscilloscope_t *osc);
void oscilloscope_set_trig_lock_cycles(oscilloscope_t *osc, uint32_t cycles);
uint32_t oscilloscope_get_trig_lock_cycles(const oscilloscope_t *osc);
// The locked sweeps follow a clock that each measured crossing moves only
// a fraction of the way, over this time constant in seconds once the lock
// has settled, so what one measurement gets wrong is averaged rather than
// shown; a fresh lock follows its first crossings closely. 0 sweeps from
// each measurement as it is.
void oscilloscope_set_trig_lock_time(oscilloscope_t *osc, float seconds);
float oscilloscope_get_trig_lock_time(const oscilloscope_t *osc);
void oscilloscope_set_channel(oscilloscope_t *osc, uint32_t channel);
uint32_t oscilloscope_get_channel(const oscilloscope_t *osc);

// stop audio streaming
void oscilloscope_stop_streaming(oscilloscope_t *osc);

// prepare for audio streaming
void oscilloscope_prepare_to_stream(oscilloscope_t *osc, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void oscilloscope_enqueue_audio_data(oscilloscope_t *osc, const void *data, uint32_t size);

#endif /* oscilloscope_h */
