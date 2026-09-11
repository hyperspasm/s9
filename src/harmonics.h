#ifndef harmonics_h
#define harmonics_h

#include "s9_dsp.h"
#include "s9_colors.h"
#include "window_function.h"
#include "harmonic_reader.h"
#include "pitch_tracker.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

// The harmonic analyzer: the latest spectrum on a linear axis scaled to the
// fundamental a pitch tracker finds, with a marker at each harmonic. While
// the tracker is locked, each harmonic that stands out from the floor
// carries its level and its phase relative to the fundamental's, and the
// fundamental's own level and the distortion the harmonics add up to sit
// above. The axis keeps the last locked fundamental, or a configured one
// before there is any, so the spectrum is always on a scale. The curve is
// live; the readings come from the spectrum averaged over a short time, so
// they settle rather than jitter.

typedef struct harmonics harmonics_t;

typedef struct {
	// analysis
	float resolution_hz;              // bin spacing; the transform is the power of two at or above
	                                  // sample_rate / resolution_hz
	window_function_t window_function;
	uint32_t channel;                 // the channel analyzed, or S9_CHANNEL_ALL for their average

	// reading the harmonics
	float averaging_s;                // time constant of the average the levels and phases come from,
	                                  // seconds; it restarts on a new note. Noise reaches a prominence
	                                  // of about 10 dB after a second of averaging, 15 dB with none
	harmonic_reader_config_t reader;  // its harmonics are the markers across the graph

	// display
	float initial_f0;                 // the axis' fundamental before the tracker has locked, Hz
	int32_t min_db, max_db;
	uint32_t major_db_interval, minor_db_interval;
	SDL_Color major_color;            // the frame, the numbers, and the axis labels
	SDL_Color minor_color;            // the grid: the dB lines and ticks, and the harmonic markers
	SDL_Color plot_color;
	float plot_width;                 // the curve's stroke, pixels
	SDL_Color readout_color;          // readings and the header while locked
	SDL_Color held_color;             // the header while not
} harmonics_config_t;

static const harmonics_config_t default_harmonics_config = {
	.resolution_hz = 2.0f,
	.window_function = WINDOW_FUNCTION_BLACKMAN_HARRIS,
	.channel = S9_CHANNEL_ALL,
	.averaging_s = 1.0f,
	.reader = {
		.harmonics = 10,
		.search_min_bins = 3,
		.search_ratio = 0.01f,
		.shoulder_bins = 32,
		.prominence_db = 10.0f,
		.hysteresis_db = 3.0f,
		.hold_s = 0.5f,
		.note_jump_cents = 50.0f,
	},
	.initial_f0 = 1000.0f,
	.min_db = -90,
	.max_db = 0,
	.major_db_interval = 10,
	.minor_db_interval = 5,
	.major_color = S9_WHITE,
	.minor_color = (SDL_Color){ .r = 0xff, .g = 0xff, .b = 0xff, .a = 0x80 },
	.plot_color = S9_LEMON,
	.plot_width = 2.0f,
	.readout_color = S9_WHITE,
	.held_color = S9_SLATE,
};

// create harmonics reading the fundamental of a tracker, which must be
// updated before each frame is rendered; NULL takes the default
// configuration
harmonics_t *harmonics_new(const harmonics_config_t *config, pitch_tracker_t *tracker);

// destroy harmonics
void harmonics_free(harmonics_t **h);

// render harmonics
void harmonics_render(harmonics_t *h, SDL_Renderer *renderer);

// the configuration, and its replacement, applied at the next frame
const harmonics_config_t *harmonics_config(const harmonics_t *h);
void harmonics_set_config(harmonics_t *h, const harmonics_config_t *config);

// the last frame's readings of harmonics 1..*count, none unless the tracker
// is locked; valid until the next frame
const harmonic_reading_t *harmonics_readings(const harmonics_t *h, uint32_t *count);

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
