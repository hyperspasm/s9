#ifndef fft_analyzer_h
#define fft_analyzer_h

#include "s9_dsp.h"
#include "window_function.h"

#include <stdint.h>

// Windowed transforms of the audio stream, once per update, over the last
// fft_len frames. Audio arrives on the audio thread through enqueue; the
// render thread calls update once per display frame and reads the spectra.
// Each update consumes the queued audio in whole display frames and slides
// the analysis window over it, so consecutive spectra overlap by all but
// one display frame's worth of samples.

typedef struct fft_analyzer fft_analyzer_t;

typedef enum {
	FFT_ANALYZER_EACH_CHANNEL,  // one transform per stream channel
	FFT_ANALYZER_MIX,           // one transform of the selected channel, or of the channels averaged
} fft_analyzer_mode_t;

typedef struct {
	uint32_t fft_len;                  // transform length; 0 derives it from resolution_hz
	float resolution_hz;               // bin spacing: the transform length is the power of two at or
	                                   // above sample_rate / resolution_hz
	window_function_t window_function;
	float window_skew;
	fft_analyzer_mode_t mode;
	uint32_t channel;                  // FFT_ANALYZER_MIX: the channel transformed, or S9_CHANNEL_ALL
	float averaging_s;                 // time constant of the per-bin power average, seconds of audio;
	                                   // 0 keeps the latest transform's
} fft_analyzer_config_t;

fft_analyzer_t *fft_analyzer_new(const fft_analyzer_config_t *config);
void fft_analyzer_free(fft_analyzer_t **fa);

// Replace the configuration. A change of transform length, resolution,
// window, or mode takes effect at the next update, which restarts the
// history at the new length; the channel and the averaging change in
// place. The audio queue is untouched either way, so the audio thread may
// keep enqueueing meanwhile.
void fft_analyzer_set_config(fft_analyzer_t *fa, const fft_analyzer_config_t *config);

// the configuration as last set, whether or not an update has applied it
const fft_analyzer_config_t *fft_analyzer_config(const fft_analyzer_t *fa);

void fft_analyzer_prepare_to_stream(fft_analyzer_t *fa, const s9_audio_stream_config_t *stream_config);
void fft_analyzer_stop_streaming(fft_analyzer_t *fa);
int fft_analyzer_streaming(const fft_analyzer_t *fa);

// audio thread: queue interleaved frames in the stream's format
void fft_analyzer_enqueue_audio_data(fft_analyzer_t *fa, const void *data, uint32_t size);

// render thread: consume the queued audio and transform
void fft_analyzer_update(fft_analyzer_t *fa);

uint32_t fft_analyzer_fft_len(const fft_analyzer_t *fa);
uint32_t fft_analyzer_sample_rate(const fft_analyzer_t *fa);
// spectra per update: the stream's channels, or 1 when mixing
uint32_t fft_analyzer_transforms(const fft_analyzer_t *fa);

// The latest transform: fft_len/2 + 1 bins as (re, im) pairs, unscaled. A
// sine of amplitude a under the normalized window has magnitude
// a * fft_len / 2 at its bin.
const float *fft_analyzer_spectrum(const fft_analyzer_t *fa, uint32_t index);

// The per-bin power, |X|^2, averaged over the configured time: each update
// moves it toward the latest transform's by a fraction set by the audio
// time the update consumed, so the average follows audio time whatever the
// update rate. Until the history has filled with audio once, it is the
// latest transform's. fft_len/2 + 1 bins.
const float *fft_analyzer_power(const fft_analyzer_t *fa, uint32_t index);

// restart the average from the next transform
void fft_analyzer_reset_power(fft_analyzer_t *fa);

// The fraction of the way the last update moved the average toward its
// transform: 1 while the history was still filling with audio (a transform
// is then of a partial frame), for the first transform after a reset, and
// with averaging off; less as the time constant says otherwise. A caller
// averaging something of its own per transform can follow the same number.
float fft_analyzer_average_fraction(const fft_analyzer_t *fa);

// the audio the last update consumed, in seconds
float fft_analyzer_hop_seconds(const fft_analyzer_t *fa);

// the window, fft_len values summing to fft_len
const float *fft_analyzer_window(const fft_analyzer_t *fa);

// RMS of the frame behind a transform, before windowing
float fft_analyzer_rms(const fft_analyzer_t *fa, uint32_t index);

#endif /* fft_analyzer_h */
