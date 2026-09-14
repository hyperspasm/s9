#ifndef conditioner_h
#define conditioner_h

#include <stddef.h>

// Turns per-frame estimates of a fundamental into a clean one: whether it
// is valid, and its frequency. An estimate is accepted when the frame's RMS
// clears the gate and the estimate's harmonicity clears a threshold, one to
// become valid and a lower one to stay so. While valid, the frequency
// follows the accepted estimates through the smoother and the dead band;
// while invalid, it holds its last value, so a display keeps a stable
// picture. The smoother is a one-pole low-pass with its time constant in
// seconds, so it averages a wobble out however fast the frames come; an
// estimate further than snap_cents from the tracked value is a new note and
// replaces it outright rather than being glided to. An estimate an octave
// from the tracked value is folded onto it, an estimator's slip, for up to
// the hold time; one that keeps insisting is a new note and is followed.
// Validity survives frames without an accepted estimate for the hold time.
// Time is the audio time between updates, so the frame rate does not
// matter. Caller-owned: allocate conditioner_alloc_size() bytes, aligned as
// malloc aligns, and init them.

typedef struct {
	float gate_rms;                // frames quieter than this have no fundamental
	float harmonicity_on;          // harmonicity needed to become valid
	float harmonicity_off;         // harmonicity needed to stay valid
	float hold_s;                  // time without an accepted estimate tolerated before validity ends, and an
	                               // octave disagreement tolerated before it is followed, seconds
	float octave_tolerance_cents;  // an estimate this close to double or half the tracked value is
	                               // folded onto it
	float smoothing_s;             // time constant of the tracked value's move toward the estimates,
	                               // seconds; the smoothing is done in cents
	float deadband_cents;          // the reported value moves only when the tracked one differs by
	                               // more
	float snap_cents;              // an estimate further than this from the tracked value is a new
	                               // note and replaces it outright
} conditioner_config_t;

typedef struct conditioner conditioner_t;

size_t conditioner_alloc_size(void);
void conditioner_init(conditioner_t *c, const conditioner_config_t *config);

// forget the tracked fundamental; the config stays
void conditioner_reset(conditioner_t *c);

// new thresholds from the next update on; the tracked fundamental stays
void conditioner_set_config(conditioner_t *c, const conditioner_config_t *config);

// One frame: the estimate in Hz (0 when there is none), its harmonicity,
// 0..1, the frame's RMS, and the audio time since the last frame. Returns
// whether the fundamental is valid; *frequency receives the reported
// fundamental in Hz, 0 before the first valid frame.
int conditioner_update(conditioner_t *c, float estimate_hz, float harmonicity, float rms, float dt_s, float *frequency);

#endif /* conditioner_h */
