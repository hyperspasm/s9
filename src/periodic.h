#ifndef periodic_h
#define periodic_h

#include "s9_dsp.h"
#include "fundamental.h"
#include "harmonics.h"
#include "oscilloscope.h"
#include "note.h"
#include "note_strip.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

// The analyzer of a periodic signal. It assumes the audio is a steady
// periodic signal, tracks its fundamental, names the note nearest it and
// how far off it is, and shows one view of the signal at a time under
// that: its harmonics, or its waveform with the sweep locked to whole
// cycles of the fundamental. The tracker and the views are its own, and
// all of them analyze the one channel it is set to.

typedef enum {
	PERIODIC_VIEW_HARMONICS,
	PERIODIC_VIEW_WAVEFORM,
	PERIODIC_VIEW_COUNT,
} periodic_view_t;

static const char *const periodic_view_names[PERIODIC_VIEW_COUNT] = {
	[PERIODIC_VIEW_HARMONICS] = "harmonics",
	[PERIODIC_VIEW_WAVEFORM]  = "waveform",
};

#define PERIODIC_CYCLES_MAX 8

typedef struct periodic periodic_t;

typedef struct {
	uint32_t channel;                 // the channel analyzed, or S9_CHANNEL_ALL for their average
	periodic_view_t view;
	float initial_f0;                 // the fundamental assumed before the tracker has locked, Hz
	fundamental_config_t tracker;     // its channel is overridden by this component's
	harmonics_config_t harmonics;     // its initial_f0 likewise
	struct {
		oscilloscope_config_t display;
		uint32_t cycles;              // of the fundamental across the sweep, at most PERIODIC_CYCLES_MAX
		float amp_per_div;
		uint32_t lock_cycles;         // of the fundamental over which its phase is measured for each sweep;
		                              // more narrows what the measurement hears around the fundamental,
		                              // and delays the sweep by as much
		float lock_time;              // seconds, the time constant of the clock the sweeps follow once it
		                              // has settled; the measured phase moves it only a fraction of the way,
		                              // so a beating partial's wobble is averaged rather than shown, while a
		                              // fresh lock follows the first crossings closely. 0 follows each
		                              // measurement as it is
	} waveform;                       // the sweep's length, trigger, and channel are set from the fundamental
	struct {
		float a4;                     // Hz, the reference the notes are named from
		note_strip_config_t strip;    // the note's display, across the top
	} note;
} periodic_config_t;

// the defaults: every channel averaged, the harmonics view, a fundamental
// of 1 kHz until one is tracked, one cycle across the waveform, notes named
// from A4 = 440 Hz, and the tracker's and the views' own defaults
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

// The tracked fundamental as a note at the configured reference. Returns
// false before the first valid frame; the note holds while the fundamental
// does.
bool periodic_note(const periodic_t *p, note_t *note);

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
