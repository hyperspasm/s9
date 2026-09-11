#ifndef harmonic_reader_h
#define harmonic_reader_h

#include "harmonic_readout.h"

#include <stddef.h>
#include <stdint.h>

// Reading the harmonics of a tracked fundamental frame by frame: which
// stand out from the floor and are shown, their levels, their phases
// relative to the fundamental, and the distortion they add up to. Levels
// come from a spectrum averaged over time, phases from the live spectrum,
// averaged here by the same fraction. Caller-owned: allocate
// harmonic_reader_alloc_size() bytes, aligned as malloc aligns, and init
// them.

typedef struct {
	uint32_t harmonics;          // read of each fundamental, counting it; at most HARMONIC_MAX
	uint32_t search_min_bins;    // a harmonic is looked for at least this far either side of its
	                             // frequency
	float search_ratio;          // and at least this fraction of that frequency, covering the reported
	                             // fundamental's dead band and smoothing lag
	uint32_t shoulder_bins;      // the floor beside a harmonic is read from this many bins per side,
	                             // fewer when the next harmonic is closer
	float prominence_db;         // a harmonic is shown once it stands this far above the floor
	                             // beside it
	float hysteresis_db;         // and stays while it stands within this of that
	float hold_s;                // and for this long after it has stopped
	float note_jump_cents;       // a fundamental moving this far between frames is a new note
} harmonic_reader_config_t;

typedef struct {
	harmonic_readout_t readout;  // levels in dB relative to a full-scale sine
	int shown;                   // what the distortion counts
	float phase_deg;             // relative to the fundamental; meaningful while shown
} harmonic_reading_t;

typedef struct harmonic_reader harmonic_reader_t;

size_t harmonic_reader_alloc_size(void);
void harmonic_reader_init(harmonic_reader_t *r, const harmonic_reader_config_t *config);
void harmonic_reader_set_config(harmonic_reader_t *r, const harmonic_reader_config_t *config);

// the window the spectra are made under, of n points; nothing is read
// before one is set
void harmonic_reader_set_window(harmonic_reader_t *r, const float *window, uint32_t n);

// forget everything read: every harmonic must earn its way back
void harmonic_reader_reset(harmonic_reader_t *r);

// One frame. db holds the averaged spectrum's levels per bin and spectrum
// the live transform as (re, im) pairs, both of an n-point transform with
// bins delta_f apart; f0 is the fundamental, locked whether it is; alpha
// is the fraction the averaged spectrum moved this frame; dt_s the audio
// time since the last frame. Without a lock nothing is read. A
// fundamental moving more than note_jump_cents since the last frame
// restarts the reader, which harmonic_reader_restarted() reports so the
// caller can restart what it averages too.
void harmonic_reader_update(harmonic_reader_t *r, const float *db, const float *spectrum, uint32_t n, double delta_f,
							double f0, int locked, float alpha, float dt_s);
int harmonic_reader_restarted(const harmonic_reader_t *r);

// the last frame's readings of harmonics 1..*count, none without a lock
const harmonic_reading_t *harmonic_reader_readings(const harmonic_reader_t *r, uint32_t *count);

// the last frame's total harmonic distortion over the harmonics shown, as
// a ratio; returns 0, or -1 when the fundamental is not shown
int harmonic_reader_thd(const harmonic_reader_t *r, float *thd);

#endif /* harmonic_reader_h */
