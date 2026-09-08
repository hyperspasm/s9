#ifndef harmonics_h
#define harmonics_h

#include "s9_dsp.h"
#include "s9_colors.h"
#include "window_function.h"
#include "harmonic_readout.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

// The harmonic analyzer: the spectrum, averaged over a short time, on a
// linear axis scaled to the tracked fundamental, with a marker at each
// harmonic and, for those that stand out from the floor, its level relative
// to the fundamental's; the fundamental's own level and the distortion the
// harmonics add up to above.

#define HARMONICS_CHANNEL_ALL UINT32_MAX

typedef struct harmonics harmonics_t;

typedef struct {
	// analysis
	float resolution_hz;              // bin spacing; the transform is the power of two at or above sample_rate / resolution_hz
	window_function_t window_function;
	uint32_t channel;                 // the channel analyzed, or HARMONICS_CHANNEL_ALL for the channels averaged

	// tracking the fundamental
	float f_min, f_max;               // the fundamentals looked for, Hz
	float gate_rms;                   // frames quieter than this are unpitched
	float clarity_on, clarity_off;    // the clarity needed to become valid, and to stay valid
	uint32_t hold_frames;             // unpitched frames tolerated before validity ends
	float octave_tolerance_cents;     // an estimate this close to double or half the tracked value is folded onto it
	float smoothing;                  // per-frame move toward the estimate, 0..1
	float deadband_cents;             // the reported value moves only when the smoothed one differs by more

	// reading the harmonics
	float averaging_s;                // time constant of the spectrum's average, seconds; the average restarts on a new note
	float prominence_db;              // a harmonic's label appears once it stands this far above the floor beside it (assumes averaging; without it, 15)
	float hysteresis_db;              // and stays while it stands within this of that
	float readout_hold_s;             // and for this long after it no longer does

	// display
	uint32_t harmonics;               // markers across the graph
	int32_t min_db, max_db;
	uint32_t major_db_interval, minor_db_interval;
	SDL_Color major_color, minor_color, plot_color;
	SDL_Color marker_color;           // markers while the fundamental is valid
	SDL_Color held_color;             // markers while it is held
	SDL_Color readout_color;
} harmonics_config_t;

static const harmonics_config_t default_harmonics_config = {
	.resolution_hz = 2.0f,
	.window_function = WINDOW_FUNCTION_BLACKMAN_HARRIS,
	.channel = HARMONICS_CHANNEL_ALL,
	.f_min = 20.0f,
	.f_max = 4000.0f,
	.gate_rms = 0.01f,
	.clarity_on = 0.9f,
	.clarity_off = 0.8f,
	.hold_frames = 3,
	.octave_tolerance_cents = 30.0f,
	.smoothing = 0.5f,
	.deadband_cents = 5.0f,
	.averaging_s = 1.0f,
	.prominence_db = 10.0f,
	.hysteresis_db = 3.0f,
	.readout_hold_s = 0.5f,
	.harmonics = 10,
	.min_db = -90,
	.max_db = 0,
	.major_db_interval = 10,
	.minor_db_interval = 5,
	.major_color = S9_WHITE,
	.minor_color = S9_WHITE,
	.plot_color = S9_ELECTRIC,
	.marker_color = S9_AMBER,
	.held_color = S9_SLATE,
	.readout_color = S9_WHITE,
};

// create harmonics; NULL takes the default configuration
harmonics_t *harmonics_new(const harmonics_config_t *config);

// destroy harmonics
void harmonics_free(harmonics_t **h);

// render harmonics
void harmonics_render(harmonics_t *h, SDL_Renderer *renderer);

// the configuration, and its replacement, applied at the next frame
const harmonics_config_t *harmonics_config(const harmonics_t *h);
void harmonics_set_config(harmonics_t *h, const harmonics_config_t *config);

// The tracked fundamental: whether it is valid now, and the frequency
// reported, which holds while invalid and is 0 before the first valid frame.
bool harmonics_get_pitch_valid(const harmonics_t *h);
float harmonics_get_pitch_frequency(const harmonics_t *h);

// The last frame's readings of harmonics 1..*count: the readout, levels in
// dB relative to a full-scale sine, and whether its label is shown, which
// is what the distortion counts. None before a fundamental is known. Valid
// until the next frame.
typedef struct {
	harmonic_readout_t readout;
	bool shown;
} harmonics_reading_t;

const harmonics_reading_t *harmonics_readings(const harmonics_t *h, uint32_t *count);

// The last frame's total harmonic distortion as a ratio. Returns 0, or -1
// when the fundamental is not shown.
int harmonics_get_thd(const harmonics_t *h, float *thd);

// stop audio streaming
void harmonics_stop_streaming(harmonics_t *h);

// prepare for audio streaming
void harmonics_prepare_to_stream(harmonics_t *h, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void harmonics_enqueue_audio_data(harmonics_t *h, const void *data, uint32_t size);

#endif /* harmonics_h */
