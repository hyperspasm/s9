#ifndef pitch_h
#define pitch_h

#include <stddef.h>
#include <stdint.h>

// Pitch estimation from a normalized autocorrelation, and the conditioning
// of per-frame estimates into a clean fundamental. Pure functions over
// caller-owned memory; nothing here allocates.

typedef struct {
	float period;    // in samples, fractional; 0 when nothing was found
	float clarity;   // the height of the chosen peak, 0..1
} pitch_estimate_t;

// The period of a frame from its normalized autocorrelation nacf[0..n-1],
// which is 1 at lag 0. The candidates are the maxima of the positive
// regions between lag_min and lag_max, past the lobe around lag 0; the
// estimate is the first candidate within tolerance of the highest, refined
// by a parabola through its neighbours. Preferring the first keeps a
// periodic signal's own period over its multiples; the tolerance lets two
// tones' common period win over the nearby peaks their beating makes. A
// region cut off by lag_max still counts. Returns zeros when no positive
// region lies in range.
pitch_estimate_t pitch_estimate(const float *nacf, uint32_t n, uint32_t lag_min, uint32_t lag_max,
								float tolerance);

typedef struct {
	float gate_rms;                // frames quieter than this are unpitched
	float clarity_on;              // clarity needed to become valid
	float clarity_off;             // clarity needed to stay valid
	float hold_s;                  // unpitched time tolerated before validity ends, and an octave
	                               // disagreement tolerated before it is followed, seconds
	float octave_tolerance_cents;  // an estimate this close to double or half the tracked value is
	                               // folded onto it
	float smoothing;               // the fraction of the way the tracked value moves toward each
	                               // estimate, 0..1; the smoothing is done in cents
	float deadband_cents;          // the reported value moves only when the tracked one differs by
	                               // more
} pitch_conditioner_config_t;

// Turns per-frame estimates into (valid, frequency). While valid, the
// frequency follows the estimates through the smoother and the deadband;
// while invalid, it holds its last value, so a display keeps a stable
// picture. An estimate an octave from the tracked value is folded onto it,
// an estimator's slip, for up to the hold time; one that keeps insisting is
// a new note and is followed. Time is the audio time between updates, so
// the frame rate does not matter. Caller-owned: allocate
// pitch_conditioner_alloc_size() bytes, aligned as malloc aligns, and init
// them.
typedef struct pitch_conditioner pitch_conditioner_t;

size_t pitch_conditioner_alloc_size(void);
void pitch_conditioner_init(pitch_conditioner_t *pc, const pitch_conditioner_config_t *config);

// forget the tracked fundamental; the config stays
void pitch_conditioner_reset(pitch_conditioner_t *pc);

// new thresholds from the next update on; the tracked fundamental stays
void pitch_conditioner_set_config(pitch_conditioner_t *pc, const pitch_conditioner_config_t *config);

// One frame: period in samples (0 when none), its clarity, the frame's
// RMS, and the audio time since the last frame. Returns whether the
// fundamental is valid; *frequency receives the reported fundamental in Hz,
// 0 before the first valid frame.
int pitch_conditioner_update(pitch_conditioner_t *pc, float period, float clarity, float rms, float sample_rate,
							 float dt_s, float *frequency);

#endif /* pitch_h */
