#ifndef spectrogram_h
#define spectrogram_h

#include "s9_dsp.h"
#include "s9_colors.h"
#include "window_function.h"
#include "palette.h"

#include <SDL2/SDL.h>

// A scrolling spectrogram: log frequency across, time down with the newest
// row at the bottom, and level as color. Each row is one hop of audio,
// hop_ms long whatever the display's frame rate, read by a constant-Q
// readout of a frame of audio: every pixel column is a band with a window
// sized for its frequency, all centred on the same instant, so the low
// frequencies are resolved to the set width at the cost of time smearing
// and the high ones keep the time resolution of a row or two, and each
// shows the strongest level within its band. The frame is as long as the
// lowest column's window needs, so the resolution holds from f_min up to
// where the shortest window takes over. Rows follow audio time, one per
// hop as it arrives, and the image is placed by a smoothed audio clock, so
// it scrolls evenly at any hop, a fraction of a row per frame. The newest
// row shows audio from half the frame ago.

typedef struct spectrogram spectrogram_t;

typedef struct {
	uint32_t channel;                // the channel analyzed, or S9_CHANNEL_ALL for their average
	uint32_t hop_ms;                 // the audio in a row, and so the scroll: one row per this many milliseconds
	uint32_t min_window_hops;        // the shortest window, in rows: 1 keeps a row's time resolution but hears a
	                                 // click faintly at a row's edge; 2 overlaps the rows by half, so a click
	                                 // reads alike wherever it falls; more evens that out further
	float resolution_semitones;      // the width of a tone's ridge at half amplitude
	window_function_t window_function;
	float min_db, max_db;            // the color scale's floor and full brightness, dB relative to a full-scale sine
	palette_t palette;               // the color scale between them
	float f_min, f_max;              // Hz, the range across the plot; the top is held within the stream's Nyquist
	SDL_Color major_color;           // labels and the decades' ticks
	SDL_Color minor_color;           // the other ticks and the frame
} spectrogram_config_t;

static const spectrogram_config_t default_spectrogram_config = {
	.channel = S9_CHANNEL_ALL,
	.hop_ms = 10,
	.min_window_hops = 2,
	.resolution_semitones = 1.0f,
	.window_function = WINDOW_FUNCTION_BLACKMAN_HARRIS,
	.min_db = -90.0f,
	.max_db = 0.0f,
	.palette = PALETTE_INFERNO,
	.f_min = 20.0f,
	.f_max = 20000.0f,
	.major_color = S9_WHITE,
	.minor_color = (SDL_Color){ .r = 0xff, .g = 0xff, .b = 0xff, .a = 0x80 },
};

// create a spectrogram; NULL takes the default configuration
spectrogram_t *spectrogram_new(const spectrogram_config_t *config);

// destroy a spectrogram
void spectrogram_free(spectrogram_t **sg);

// render the image and its axes into the viewport
void spectrogram_render(spectrogram_t *sg, SDL_Renderer *renderer);

// the configuration, and its replacement, applied at the next frame
const spectrogram_config_t *spectrogram_config(const spectrogram_t *sg);
void spectrogram_set_config(spectrogram_t *sg, const spectrogram_config_t *config);

// Rows written since the image last started over, which it does when
// streaming is prepared, when the frame or the readout changes, and when
// the viewport does. Every hop of audio after the frame has filled once is
// a row.
uint32_t spectrogram_rows(const spectrogram_t *sg);

// The time resolution the settings give, which varies with frequency: the
// frame's length in samples and in seconds, the window of the lowest
// column and the longest smear any event can have; and the frequency from
// which the shortest window rules, above which the smear is that window's
// and the ridges are narrower than the resolution says. Between the two the
// smear follows the resolution, doubling with every octave down. 0 before
// streaming.
uint32_t spectrogram_frame(const spectrogram_t *sg);
float spectrogram_time_resolution(const spectrogram_t *sg);
float spectrogram_floor_frequency(const spectrogram_t *sg);

// stop audio streaming
void spectrogram_stop_streaming(spectrogram_t *sg);

// prepare for audio streaming
void spectrogram_prepare_to_stream(spectrogram_t *sg, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void spectrogram_enqueue_audio_data(spectrogram_t *sg, const void *data, uint32_t size);

#endif /* spectrogram_h */
