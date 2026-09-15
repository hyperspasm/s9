#ifndef constant_q_h
#define constant_q_h

#include "window_function.h"

#include <stdint.h>

// A constant-Q readout of a frame of audio: the level in each of a set of
// frequency bands, the columns, through a window sized for the band's
// frequency, every window centred on the middle of the frame. A column's
// window is as long as it takes for a tone's ridge through it to be
// resolution semitones wide at half amplitude at the band's centre, no
// shorter than min_window and no longer than the frame, so the low columns
// trade time for frequency resolution and the high ones the reverse. The
// column is read at frequencies spread across its band, as many as it
// takes for them to be no farther apart than the ridge's width over
// readouts_per_ridge, and shows the largest, so a tone anywhere in the band
// reads within a fraction of a dB of its level. Each readout is the
// windowed transform at exactly its frequency, scaled so that a full-scale
// sine reads 0 dB, computed as Brown and Puckette (1992) do: a transform of
// the frame dotted with the transform of the window shifted to that
// frequency, kept where it stands above threshold_db of its peak. The
// transform is the shortest that holds the readout's window, the frame or
// its middle at a power of two, so the kernels stay a few dozen taps
// however long the frame is. The oracle is validation/src/s9/constant_q.py.

typedef struct constant_q constant_q_t;

typedef struct {
	double sample_rate;
	uint32_t n;                        // the frame's length, even: the longest window
	uint32_t min_window;               // the shortest window, frames
	float resolution_semitones;        // the width of a tone's ridge at half amplitude
	window_function_t window_function;
	float threshold_db;                // a kernel's taps below this of its peak are dropped
	float readouts_per_ridge;          // readouts across a band are at most the ridge's width over this apart
} constant_q_config_t;

// Columns given as the edges between them: columns + 1 ascending
// frequencies in Hz. Builds one transform per readout, most of them short.
constant_q_t *constant_q_new(const constant_q_config_t *config, const double *edges, uint32_t columns);
void constant_q_free(constant_q_t **cq);

uint32_t constant_q_columns(const constant_q_t *cq);
uint32_t constant_q_window(const constant_q_t *cq, uint32_t column);     // a column's window length, frames
uint32_t constant_q_readouts(const constant_q_t *cq, uint32_t column);   // the frequencies a column is read at
uint32_t constant_q_taps(const constant_q_t *cq);                        // taps in all: the cost of a readout

// The columns' levels in dB relative to a full-scale sine, from a frame of
// n samples, the audio around the instant the columns are read at.
void constant_q_readout(constant_q_t *cq, const float *frame, float *out_db);

#endif /* constant_q_h */
