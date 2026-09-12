#ifndef fundamental_h
#define fundamental_h

#include "s9_dsp.h"
#include "window_function.h"
#include "period.h"

#include <stdbool.h>
#include <stdint.h>

// The fundamental of the audio, frame by frame: a windowed transform, the
// period from its normalized autocorrelation, and the conditioner that
// turns the estimates into a valid flag and a frequency which holds while
// invalid. A component that draws nothing; others read what it tracks, and
// may read the transform it tracks from, its latest bins and their power
// averaged over time, rather than run one of their own.

typedef struct fundamental fundamental_t;

typedef struct {
	float window_s;                   // the audio analyzed at a time, seconds: the transform is the longest
	                                  // power of two of samples within it. A window shorter than a tremolo's
	                                  // cycle lets the envelope mislead the estimate to a sub-multiple of the
	                                  // fundamental; several cycles in the window average it out. Shorter follows
	                                  // a change sooner
	window_function_t window_function;
	uint32_t channel;                 // the channel analyzed, or S9_CHANNEL_ALL for their average
	float averaging_s;                // time constant of the power average, seconds of audio; 0 keeps the
	                                  // latest transform's. The tracking does not use it; readers of the
	                                  // power do
	float f_min, f_max;               // the fundamentals looked for, Hz
	period_conditioner_config_t conditioner;
} fundamental_config_t;

static const fundamental_config_t default_fundamental_config = {
	.window_s = 0.75f,
	.window_function = WINDOW_FUNCTION_BLACKMAN_HARRIS,
	.channel = S9_CHANNEL_ALL,
	.averaging_s = 1.0f,
	.f_min = 20.0f,
	.f_max = 4000.0f,
	.conditioner = {
		.gate_rms = 0.01f,
		.clarity_on = 0.9f,
		.clarity_off = 0.8f,
		.hold_s = 0.05f,
		.octave_tolerance_cents = 30.0f,
		.smoothing_s = 0.2f,
		.deadband_cents = 5.0f,
	},
};

// create a tracker; NULL takes the default configuration
fundamental_t *fundamental_new(const fundamental_config_t *config);

// destroy a tracker
void fundamental_free(fundamental_t **t);

// the configuration, and its replacement, applied at the next update
const fundamental_config_t *fundamental_config(const fundamental_t *t);
void fundamental_set_config(fundamental_t *t, const fundamental_config_t *config);

// render thread, once per display frame: consume the queued audio and track
void fundamental_update(fundamental_t *t);

// The fundamental as of the last update: whether it is valid now, and the
// frequency reported, which holds while invalid and is 0 before the first
// valid frame.
bool fundamental_valid(const fundamental_t *t);
float fundamental_frequency(const fundamental_t *t);

// The transform as of the last update, for readers of the spectrum. The
// bins are the latest transform as (re, im) pairs, fft_len/2 + 1 of them,
// unscaled: a sine of amplitude a under the normalized window has magnitude
// a * fft_len / 2 at its bin. The power is |X|^2 per bin averaged over the
// configured time, each update moving it toward the latest transform's by
// the fraction reported, 1 while the history was still filling or after a
// restart. The window is fft_len values summing to fft_len.
int fundamental_streaming(const fundamental_t *t);
uint32_t fundamental_fft_len(const fundamental_t *t);
uint32_t fundamental_sample_rate(const fundamental_t *t);
const float *fundamental_bins(const fundamental_t *t);
const float *fundamental_power(const fundamental_t *t);
float fundamental_average_fraction(const fundamental_t *t);
const float *fundamental_window(const fundamental_t *t);
window_function_t fundamental_window_function(const fundamental_t *t);
float fundamental_hop_seconds(const fundamental_t *t);

// restart the power average from the next transform, as a reader may ask
// when what it reads has changed
void fundamental_restart_average(fundamental_t *t);

// stop audio streaming
void fundamental_stop_streaming(fundamental_t *t);

// prepare for audio streaming
void fundamental_prepare_to_stream(fundamental_t *t, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void fundamental_enqueue_audio_data(fundamental_t *t, const void *data, uint32_t size);

#endif /* fundamental_h */
