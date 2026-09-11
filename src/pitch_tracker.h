#ifndef pitch_tracker_h
#define pitch_tracker_h

#include "s9_dsp.h"
#include "window_function.h"
#include "pitch.h"

#include <stdbool.h>
#include <stdint.h>

// The fundamental of the audio, frame by frame: a windowed transform of
// its own, the period from the normalized autocorrelation, and the
// conditioner that turns the estimates into a valid flag and a frequency
// which holds while invalid. A component that draws nothing; others read
// what it tracks.

typedef struct pitch_tracker pitch_tracker_t;

typedef struct {
	float resolution_hz;              // bin spacing of the transform; the transform is the power of two
	                                  // at or above sample_rate / resolution_hz
	window_function_t window_function;
	uint32_t channel;                 // the channel analyzed, or S9_CHANNEL_ALL for their average
	float f_min, f_max;               // the fundamentals looked for, Hz
	pitch_conditioner_config_t conditioner;
} pitch_tracker_config_t;

static const pitch_tracker_config_t default_pitch_tracker_config = {
	.resolution_hz = 2.0f,
	.window_function = WINDOW_FUNCTION_BLACKMAN_HARRIS,
	.channel = S9_CHANNEL_ALL,
	.f_min = 20.0f,
	.f_max = 4000.0f,
	.conditioner = {
		.gate_rms = 0.01f,
		.clarity_on = 0.9f,
		.clarity_off = 0.8f,
		.hold_s = 0.05f,
		.octave_tolerance_cents = 30.0f,
		.smoothing = 0.5f,
		.deadband_cents = 5.0f,
	},
};

// create a tracker; NULL takes the default configuration
pitch_tracker_t *pitch_tracker_new(const pitch_tracker_config_t *config);

// destroy a tracker
void pitch_tracker_free(pitch_tracker_t **t);

// the configuration, and its replacement, applied at the next update
const pitch_tracker_config_t *pitch_tracker_config(const pitch_tracker_t *t);
void pitch_tracker_set_config(pitch_tracker_t *t, const pitch_tracker_config_t *config);

// render thread, once per display frame: consume the queued audio and track
void pitch_tracker_update(pitch_tracker_t *t);

// The fundamental as of the last update: whether it is valid now, and the
// frequency reported, which holds while invalid and is 0 before the first
// valid frame.
bool pitch_tracker_valid(const pitch_tracker_t *t);
float pitch_tracker_frequency(const pitch_tracker_t *t);

// stop audio streaming
void pitch_tracker_stop_streaming(pitch_tracker_t *t);

// prepare for audio streaming
void pitch_tracker_prepare_to_stream(pitch_tracker_t *t, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void pitch_tracker_enqueue_audio_data(pitch_tracker_t *t, const void *data, uint32_t size);

#endif /* pitch_tracker_h */
