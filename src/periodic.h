#ifndef periodic_h
#define periodic_h

#include "s9_dsp.h"
#include "tracker.h"
#include "frequency_counter.h"
#include "fft_analyzer.h"
#include "harmonics.h"
#include "oscilloscope.h"
#include "note.h"
#include "note_strip.h"
#include "tuner_strip.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

// The analyzer of a periodic signal. It assumes the audio is a steady or
// steadily moving periodic signal, tracks its fundamental in two stages, the
// lock and the fundamental decided from it, and names the note nearest the
// fundamental and how far off it is. Across the top it shows the note on
// the range it tracks, with the lock as a dim marker beside it, and its
// cents on a tuner's scale; under them, one view of the signal at a time:
// its harmonics, read from a transform of its own, or its waveform with the
// sweep locked to whole cycles of the fundamental. Beside the tracker a
// frequency counter reads the same audio by its zero crossings, a reference
// for waveforms that cross once per cycle. The tracker, the counter, the
// transform, the strips, and the views are its own, and all of them analyze
// the one channel it is set to.

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
	float initial_f0;                 // the fundamental assumed before one has been decided, Hz
	tracker_config_t tracker;         // the tracker's; a change remakes the tracker, which starts over
	frequency_counter_config_t counter;   // the frequency counter's; a change likewise remakes it
	struct {
		float window_s;               // the audio the harmonics view's transform spans, seconds: the transform
		                              // is the longest power of two of samples within it. Longer resolves the
		                              // partials more finely; shorter follows a change sooner
		window_function_t window_function;
		float averaging_s;            // time constant of its power average, seconds of audio, which the
		                              // harmonics' readings settle over; 0 keeps the latest transform's
	} spectrum;
	harmonics_config_t harmonics;     // its initial_f0 is overridden by this component's
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
		note_strip_config_t range;    // the note on the range tracked, across the top
		tuner_strip_config_t tuner;   // and its cents on a tuner's scale, under that
	} note;
} periodic_config_t;

// the defaults: every channel averaged, the harmonics view, a fundamental
// of 1 kHz until one is decided, a 0.75 s Blackman-Harris transform averaged
// over a second for the harmonics, one cycle across the waveform, notes
// named from A4 = 440 Hz, and the tracker's and the views' own defaults
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

// The tracked fundamental: whether it is valid now, that is, decided and
// its lock held; the frequency reported, which holds while invalid and is
// 0 before the first valid frame; and the harmonicity behind the decision,
// the comb's share of the evident power at the last evaluation.
bool periodic_valid(const periodic_t *p);
float periodic_frequency(const periodic_t *p);
float periodic_harmonicity(const periodic_t *p);

// The lock: whether there is one now; its frequency in Hz, 0 without one;
// and its confidence, the last hop's correlation, 0 while coasting.
bool periodic_lock(const periodic_t *p, float *frequency, float *confidence);

// The frequency counter's reading of the same audio, as frequency_counter.h
// has it: whether it is valid, and the frequency in Hz, 0 when it is not.
bool periodic_counter(const periodic_t *p, float *frequency);

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
