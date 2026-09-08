#ifndef harmonic_readout_h
#define harmonic_readout_h

#include <stdint.h>

// Per-harmonic readouts from a spectrum: the level and the frequency of the
// peak nearest a harmonic, refined between bins. Pure functions over
// caller-owned memory.

// Magnitudes in dB relative to a full-scale sine: under a window normalized
// to sum to n, a sine of amplitude a reads 20 log10(a) at its bin. spectrum
// holds bins (re, im) pairs of an n-point transform; db receives bins values,
// -inf for an empty bin.
void harmonic_magnitude_db(const float *spectrum, uint32_t bins, uint32_t n, float *db);

// A parabola through three dB values is not the shape of a window's main
// lobe, so refining a peak with one is biased by an amount that depends only
// on the window and on the offset the parabola reports. The table holds that
// bias over reported offsets 0..0.5, uniformly.
#define HARMONIC_CORRECTION_POINTS 33
typedef struct {
	double db_bias[HARMONIC_CORRECTION_POINTS];       // the parabola's peak is high by this
	double offset_bias[HARMONIC_CORRECTION_POINTS];   // the parabola's offset is off by this, away from the true peak
} harmonic_correction_t;

// the table for a window of n samples
void harmonic_correction_compute(harmonic_correction_t *c, const float *window, uint32_t n);

typedef struct {
	int found;         // a peak lies within reach of the harmonic
	float frequency;   // Hz
	float db;
} harmonic_readout_t;

// The peak within search bins of centre_hz: the loudest bin there, refined
// by a parabola through its neighbours and corrected. delta_f is the bin
// spacing; db holds bins values. Not found when the loudest bin is no local
// maximum (the skirt of something louder nearby has no peak to read), or
// when the nearest bin is the first or lies beyond the last a parabola fits
// around.
harmonic_readout_t harmonic_readout(const float *db, uint32_t bins, double delta_f, double centre_hz, uint32_t search,
									const harmonic_correction_t *c);

#endif /* harmonic_readout_h */
