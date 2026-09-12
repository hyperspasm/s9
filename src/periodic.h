#ifndef periodic_h
#define periodic_h

#include "s9_dsp.h"
#include "fundamental.h"
#include "harmonics.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

// The analyzer of a periodic signal. It assumes the audio is a steady
// periodic signal, tracks its fundamental, and shows one view of it at a
// time: its harmonics now, its waveform locked to one cycle to come. The
// tracker and the views are its own, and all of them analyze the one
// channel it is set to.

typedef enum {
	PERIODIC_VIEW_HARMONICS,
	PERIODIC_VIEW_COUNT,
} periodic_view_t;

static const char *const periodic_view_names[PERIODIC_VIEW_COUNT] = {
	[PERIODIC_VIEW_HARMONICS] = "harmonics",
};

typedef struct periodic periodic_t;

typedef struct {
	uint32_t channel;                 // the channel analyzed, or S9_CHANNEL_ALL for their average
	periodic_view_t view;
	fundamental_config_t tracker;   // its channel is overridden by this component's
	harmonics_config_t harmonics;
} periodic_config_t;

// the defaults: every channel averaged, the harmonics view, and the
// tracker's and the view's own defaults
periodic_config_t periodic_default_config(void);

// create a periodic analyzer; NULL takes the default configuration
periodic_t *periodic_new(const periodic_config_t *config);

// destroy a periodic analyzer
void periodic_free(periodic_t **p);

// render the view
void periodic_render(periodic_t *p, SDL_Renderer *renderer);

// the configuration, and its replacement, applied at the next frame
const periodic_config_t *periodic_config(const periodic_t *p);
void periodic_set_config(periodic_t *p, const periodic_config_t *config);

// The tracked fundamental: whether it is valid now, and the frequency
// reported, which holds while invalid and is 0 before the first valid frame.
bool periodic_valid(const periodic_t *p);
float periodic_frequency(const periodic_t *p);

// the harmonics view's last readings and distortion, as harmonics.h has them
const harmonic_reading_t *periodic_readings(const periodic_t *p, uint32_t *count);
int periodic_thd(const periodic_t *p, float *thd);

// stop audio streaming
void periodic_stop_streaming(periodic_t *p);

// prepare for audio streaming
void periodic_prepare_to_stream(periodic_t *p, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void periodic_enqueue_audio_data(periodic_t *p, const void *data, uint32_t size);

#endif /* periodic_h */
