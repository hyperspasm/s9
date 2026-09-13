#ifndef pll_h
#define pll_h

#include <stddef.h>

// A phase-locked loop over the times of a periodic event. The loop is a
// clock with a period and a phase: every event is expected at
// phase + n * period. Each measured time moves the phase a fraction of the
// way toward it and corrects the period by a smaller fraction of the same
// error, the fractions set by a time constant and the time elapsed since
// the last measurement, the period's fraction chosen for a damping ratio
// of 0.7, so the clock settles without ringing. The time constant grows
// with the lock's age until it reaches the configured one: a fresh lock
// follows its first measurements closely and measures the period from
// them, then smooths more as it ages. So a period that starts off
// converges within a few events, the clock settles on the train's
// average, and only a fraction of any one measurement's error is passed
// on. A measurement too far from its prediction is a slip: the clock
// re-seats its phase on it and acquires again from there, its period as
// the starting point. Times and the time constant share the caller's
// unit. Caller-owned: allocate pll_alloc_size() bytes, aligned as malloc
// aligns, and init them.

typedef struct {
	double tc;        // time constant of the settled loop; 0 follows each measurement all the way
	double capture;   // a measurement further than this fraction of the period from its
	                  // prediction is a slip
} pll_config_t;

typedef struct pll pll_t;

size_t pll_alloc_size(void);
void pll_init(pll_t *p, const pll_config_t *config);
void pll_set_config(pll_t *p, const pll_config_t *config);

// forget the phase and the period
void pll_reset(pll_t *p);

// the period to start from; the next observation seats the phase
void pll_seed(pll_t *p, double period);

// whether the clock has a phase: seeded, and observed since
int pll_locked(const pll_t *p);
double pll_period(const pll_t *p);

// one measured event time; returns its error against the prediction, 0
// when it seats the phase or there is no period to lock to
double pll_observe(pll_t *p, double t);

// the first predicted event at or after a time; after itself while unlocked
double pll_next(const pll_t *p, double after);

#endif /* pll_h */
