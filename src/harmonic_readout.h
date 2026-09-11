#ifndef harmonic_readout_h
#define harmonic_readout_h

#include <stdint.h>

// Reading harmonics from a spectrum: what the window does to a peak, the
// level, frequency, and prominence of the peak nearest a harmonic, its
// phase relative to the fundamental, the distortion the harmonics add up
// to, and the gate that decides whether a harmonic's reading is shown.
// Pure functions over caller-owned memory.

// Magnitudes in dB relative to a full-scale sine: under a window normalized
// to sum to n, a sine of amplitude a reads 20 log10(a) at its bin. spectrum
// holds bins (re, im) pairs of an n-point transform; db receives bins values,
// -inf for an empty bin.
void harmonic_magnitude_db(const float *spectrum, uint32_t bins, uint32_t n, float *db);

// the same scale from per-bin power |X|^2, the form an average over time keeps
void harmonic_power_db(const float *power, uint32_t bins, uint32_t n, float *db);

// What the readouts need to know about the window. A parabola through
// three dB values is not the shape of a window's main lobe, so refining a
// peak with one is biased by an amount that depends only on the window and
// on the offset the parabola reports; the tables hold that bias over
// reported offsets 0..0.5, uniformly. The reach is where a peak's own lobe
// no longer raises its neighbours, wherever the peak sits within its bin.
#define HARMONIC_CORRECTION_POINTS 33
typedef struct {
	double db_bias[HARMONIC_CORRECTION_POINTS];       // the parabola's peak is high by this
	double offset_bias[HARMONIC_CORRECTION_POINTS];   // the parabola's offset is off by this, away from the true peak
	uint32_t reach;                                   // bins from a peak's bin to the first bin outside its main lobe
} harmonic_window_t;

// the model of a window of n samples
void harmonic_window_compute(harmonic_window_t *w, const float *window, uint32_t n);

typedef struct {
	int peak;          // a local maximum lies there: frequency, db, and prominence are read
	int found;         // and it stands the prominence asked for above the floor
	float prominence;  // dB above the floor; -inf without a peak
	float frequency;   // Hz
	float db;
} harmonic_readout_t;

// The most bins the floor is read from on each side of a peak
#define HARMONIC_SHOULDER_MAX 64

// The harmonic within search bins of centre_hz: the loudest bin there, when
// it is a local maximum standing prominence dB above the floor, refined by
// a parabola through its neighbours and corrected. The floor is, on each
// side, the median of the shoulder bins starting the window's reach away;
// the lower side's, so the skirt of a louder neighbour does not hide a
// harmonic. The caller keeps the shoulder short enough that it ends before
// the neighbouring harmonic's lobe. delta_f is the bin spacing; db holds
// bins values. Not found either when the nearest bin is the first or lies
// beyond the last a parabola fits around.
harmonic_readout_t harmonic_readout(const float *db, uint32_t bins, double delta_f, double centre_hz,
									uint32_t search, uint32_t shoulder, float prominence,
									const harmonic_window_t *w);

// Harmonic k's phase relative to the fundamental's, in degrees in
// (-180, 180]: its phase less k times the fundamental's, which is the shape
// of the waveform whatever its time origin. The phases are read at the peak
// bins p_k and p_1 of the n-point spectrum, at the frame's centre, where a
// symmetric window adds no phase of its own within its main lobe.
float harmonic_phase(const float *spectrum, uint32_t n, uint32_t p_k, uint32_t p_1, uint32_t k);

// Total harmonic distortion over readouts of harmonics 1..count: the RMS of
// the harmonics found over the fundamental's, as a ratio. Returns 0, or -1
// when the fundamental was not found.
int harmonic_thd(const harmonic_readout_t *reads, uint32_t count, float *thd);

// The gate behind one harmonic's label: it opens when the readout's
// prominence reaches on_db, stays open while the prominence holds off_db,
// and closes once it has been below that for hold_s seconds. dt_s is the
// audio time since the last update. Zero-initialize; returns whether open.
typedef struct {
	int open;
	float below_s;   // time spent under off_db while open
} harmonic_gate_t;

int harmonic_gate_update(harmonic_gate_t *g, const harmonic_readout_t *r, float on_db, float off_db,
						 float hold_s, float dt_s);

#endif /* harmonic_readout_h */
