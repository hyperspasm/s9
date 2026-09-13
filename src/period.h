#ifndef period_h
#define period_h

#include <stddef.h>
#include <stdint.h>

// Period estimation from a normalized autocorrelation, and the conditioning
// of per-frame estimates into a clean fundamental. Pure functions over
// caller-owned memory; nothing here allocates.

typedef struct {
	float period;    // in samples, fractional; 0 when nothing was found
	float clarity;   // the height of the chosen peak, 0..1
} period_estimate_t;

// The period of a frame from its normalized autocorrelation nacf[0..n-1],
// which is 1 at lag 0. The candidates are the maxima of the positive
// regions between lag_min and lag_max, past the lobe around lag 0; the
// estimate is the first candidate within tolerance of the highest, refined
// by a parabola through its neighbours. Preferring the first keeps a
// periodic signal's own period over its multiples; the tolerance lets two
// tones' common period win over the nearby peaks their beating makes. A
// region cut off by lag_max still counts. Returns zeros when no positive
// region lies in range.
period_estimate_t period_estimate(const float *nacf, uint32_t n, uint32_t lag_min, uint32_t lag_max,
								float tolerance);

typedef struct {
	float gate_rms;                // frames quieter than this have no fundamental
	float harmonicity_on;          // harmonicity needed to become valid
	float harmonicity_off;         // harmonicity needed to stay valid
	float hold_s;                  // time without a fundamental tolerated before validity ends, and an octave
	                               // disagreement tolerated before it is followed, seconds
	float octave_tolerance_cents;  // an estimate this close to double or half the tracked value is
	                               // folded onto it
	float smoothing_s;             // time constant of the tracked value's move toward the estimates,
	                               // seconds; the smoothing is done in cents
	float deadband_cents;          // the reported value moves only when the tracked one differs by
	                               // more
} period_conditioner_config_t;

// Turns per-frame estimates into (valid, frequency). While valid, the
// frequency follows the estimates through the smoother and the deadband;
// while invalid, it holds its last value, so a display keeps a stable
// picture. The smoother is a one-pole low-pass with its time constant in
// seconds, so it averages a vibrato out and glides to a new note however
// fast the frames come. An estimate an octave from the tracked value is folded onto it,
// an estimator's slip, for up to the hold time; one that keeps insisting is
// a new note and is followed. Time is the audio time between updates, so
// the frame rate does not matter. Caller-owned: allocate
// period_conditioner_alloc_size() bytes, aligned as malloc aligns, and init
// them.
typedef struct period_conditioner period_conditioner_t;

size_t period_conditioner_alloc_size(void);
void period_conditioner_init(period_conditioner_t *pc, const period_conditioner_config_t *config);

// forget the tracked fundamental; the config stays
void period_conditioner_reset(period_conditioner_t *pc);

// new thresholds from the next update on; the tracked fundamental stays
void period_conditioner_set_config(period_conditioner_t *pc, const period_conditioner_config_t *config);

// One frame: period in samples (0 when none), the harmonicity of the
// estimate it came from, 0..1, the frame's RMS, and the audio time since
// the last frame. Returns whether the fundamental is valid; *frequency
// receives the reported fundamental in Hz, 0 before the first valid frame.
int period_conditioner_update(period_conditioner_t *pc, float period, float harmonicity, float rms, float sample_rate,
							 float dt_s, float *frequency);

#endif /* period_h */
