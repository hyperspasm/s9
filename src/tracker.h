#ifndef tracker_h
#define tracker_h

#include <stdbool.h>
#include <stdint.h>

// The two-stage fundamental tracker validation/tracker/objective.md
// describes, over a mono stream of samples. The lock follows a periodicity:
// found over the whole range by YIN's normalized difference when consecutive
// frames agree, then followed hop by hop by the best lag of a window of a few
// periods against itself, the window shortening while the pitch moves;
// coasted through hops without a usable lag for a hold time, during which the
// search runs near the lost period; stamped at the middle of the sample pairs
// its correlation compared; and reported through a smoother that is slow while
// the lock is still and fast while it moves. The fundamental is the lock times
// a small rational, decided from a spectral frame warped by the lock's own
// history so its partials stay sharp through motion: the lock's comb, an octave
// sub-multiple whose own slots hold persistent evidence, a multiple the
// evident harmonic numbers' greatest common divisor points to, or the louder
// of two voices sharing the comb; committed once the comb holds most of the
// evident power for a few evaluations, revised when another decision has held
// for longer, lapsing only after a while without evidence. Its value is the
// lock over the decision's ratio, corrected slowly toward a least-squares fit
// of the partials on the comb. Samples are fed in any lengths; every complete
// hop is analyzed as it arrives, and the state is as of the last hop.

typedef struct tracker tracker_t;

typedef struct {
	float hop_s;                   // the analysis step, seconds of audio
	float f_min, f_max;            // fundamentals looked for, Hz
	float gate_dbfs;               // hops quieter than this are silence
	// the lock
	float acquire_short_s;         // the frames the global search runs on, short first: each finds periods up to
	float acquire_long_s;          // a third of itself, the short one serving most of the range quickly
	float acquire_threshold;       // YIN's threshold on the normalized difference
	uint32_t acquire_agree;        // consecutive frames agreeing before locking, at most TRACKER_AGREE_MAX
	float acquire_tolerance;       // their agreement, as a fraction of the period
	float periods_in_window;       // the tracking window, in periods, within the bounds
	float window_min_s, window_max_s;
	float window_drift;            // the window is shortened so the pitch moves less than this across it
	float search;                  // lags searched, as a fraction of the period either way
	float track_min_corr;          // a correlation peak below this is a miss
	float hold_s;                  // misses coasted through before the lock is lost
	float smooth_max_s;            // the reported lock is smoothed over up to this long while still...
	float smooth_drift_cents;      // ...and over less while moving, so the smoothing lags by no more than about this
	// the fundamental
	float cycles_in_frame;         // the spectral frame, in cycles of the lock, within the bounds, which are
	uint32_t frame_min, frame_max; // powers of two: enough that a partial's shoulders lie clear of its neighbours
	float prominence_db;           // a peak stands this far above the floor beside it
	uint32_t shoulder;             // bins of floor read on each side of a peak, past the window's lobe
	uint32_t lobe_reach;           // bins a Blackman-Harris peak raises on each side
	float peak_floor_db;           // peaks this far below the strongest are ignored
	float coarse_fraction;         // a peak this near a harmonic of the lock, as a fraction, is on the coarse comb
	uint32_t refine_harmonics;     // the lowest harmonics on it refine the comb's fundamental
	float on_comb_bins;            // a peak this near a harmonic, in bins, plus a fraction of it...
	float on_comb_fraction;        // ...plus the stretch a string would give it, is on the refined comb
	float inharmonicity_max;       // the stretch allowed: a string's B at middle C, growing in proportion to the pitch
	float slot_fraction;           // but a slot never reaches past this fraction of the way to its neighbours
	float comb_span;               // peaks up to this many times the lock's frequency take part
	float balance_db;              // evidence of a series is a peak within this of the strongest peak...
	float evidence_prominence_db;  // ...standing this far above the floor beside it
	float evidence_span;           // a sub-multiple's evidence lies below this many times the lock's frequency
	float persistence;             // a sub-multiple's evidence, or a lift's, counts once present in this fraction of
	float persistence_leak;        // recent evaluations, leakily counted with this step per evaluation...
	float pending;                 // ...and while it has built past this, the lock's own comb waits
	uint32_t lift_min_peaks;       // a lift rests on at least this many evident peaks
	float harmonicity_on;          // the comb's share of the evident peaks' power, to commit
	float harmonicity_off;         // and to stay committed
	float lapse_s;                 // a committed decision lapses after this long below harmonicity_off
	uint32_t commit_evals;         // evaluations a decision must hold before it commits
	uint32_t revise_evals;         // and a different one, net of contrary ones, before it replaces a committed decision
	float correction_tc_s;         // the lock's correction toward the fit follows with this time constant
	float correction_max;          // and never exceeds this fraction
	uint32_t seed_peaks;           // a decision starts from the fit's correction when the fit rests on this many peaks
} tracker_config_t;

#define TRACKER_AGREE_MAX 8

static const tracker_config_t default_tracker_config = {
	.hop_s = 0.01f,
	.f_min = 30.0f,
	.f_max = 2400.0f,
	.gate_dbfs = -75.0f,
	.acquire_short_s = 0.032f,
	.acquire_long_s = 0.096f,
	.acquire_threshold = 0.2f,
	.acquire_agree = 2,
	.acquire_tolerance = 0.06f,
	.periods_in_window = 6.0f,
	.window_min_s = 0.025f,
	.window_max_s = 0.12f,
	.window_drift = 0.03f,
	.search = 0.12f,
	.track_min_corr = 0.5f,
	.hold_s = 0.15f,
	.smooth_max_s = 0.15f,
	.smooth_drift_cents = 3.0f,
	.cycles_in_frame = 16.0f,
	.frame_min = 4096,
	.frame_max = 16384,
	.prominence_db = 12.0f,
	.shoulder = 12,
	.lobe_reach = 4,
	.peak_floor_db = 60.0f,
	.coarse_fraction = 0.03f,
	.refine_harmonics = 4,
	.on_comb_bins = 0.15f,
	.on_comb_fraction = 0.004f,
	.inharmonicity_max = 4e-4f,
	.slot_fraction = 0.25f,
	.comb_span = 12.0f,
	.balance_db = 40.0f,
	.evidence_prominence_db = 15.0f,
	.evidence_span = 2.0f,
	.persistence = 0.6f,
	.persistence_leak = 0.15f,
	.pending = 0.3f,
	.lift_min_peaks = 2,
	.harmonicity_on = 0.6f,
	.harmonicity_off = 0.3f,
	.lapse_s = 1.0f,
	.commit_evals = 3,
	.revise_evals = 40,
	.correction_tc_s = 0.5f,
	.correction_max = 0.02f,
	.seed_peaks = 4,
};

typedef struct {
	bool locked;
	float lock_hz;             // the lock's frequency as reported; 0 while unlocked
	float lock_confidence;     // the last hop's correlation at the lock; 0 while coasting or unlocked
	bool committed;
	float fundamental_hz;      // the fundamental; 0 while undecided or unlocked
	float harmonicity;         // the comb's share of the evident power at the last evaluation
	float delay_s;             // how far before the newest sample fed the reported lock stands
} tracker_state_t;

// a tracker for a sample rate; NULL takes the default configuration
tracker_t *tracker_new(const tracker_config_t *config, uint32_t sample_rate);
void tracker_free(tracker_t **t);

// forget everything heard, as at a stream's start
void tracker_reset(tracker_t *t);

// feed mono samples: each hop completed is analyzed; returns how many were
uint32_t tracker_feed(tracker_t *t, const float *samples, uint32_t count);

// the state as of the last hop analyzed
tracker_state_t tracker_state(const tracker_t *t);

// the hop, in samples
uint32_t tracker_hop(const tracker_t *t);

// the lock's workings as of the last hop, for tools that trace it: its period in
// samples, its rate of change per hop, that rate's magnitude smoothed, and the
// tracking window in samples
void tracker_lock_internals(const tracker_t *t, double *period, double *rate, double *slew, uint32_t *window);

// the fundamental's workings as of the last hop: the committed decision's numerator and
// denominator (0 and 0 while undecided), how much of the recent evidence each octave
// sub-multiple has held (two values, for the octave and the double octave), and the
// peaks the last frame gave
void tracker_fundamental_internals(const tracker_t *t, int *g, int *m, double *sub_seen, uint32_t *peaks);

#endif /* tracker_h */
