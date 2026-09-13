#include "pll.h"

#include <math.h>

struct pll {
	pll_config_t config;
	int locked;
	double period;
	double phase;     // the time of one event; every event lies at phase + n * period
	double last;      // the last measurement's time
	double seated;    // when the phase was last seated: the lock's age counts from here
};

// a lock's time constant is its age over this, until the configured one is smaller
#define PLL_ACQUISITION 4.0

// half away from zero, the same rounding wherever the loop counts cycles
static double round_half_away(double x) {
	return x >= 0.0 ? floor(x + 0.5) : -floor(-x + 0.5);
}

size_t pll_alloc_size(void) {
	return sizeof(pll_t);
}

void pll_init(pll_t *p, const pll_config_t *config) {
	p->config = *config;
	pll_reset(p);
}

void pll_set_config(pll_t *p, const pll_config_t *config) {
	p->config = *config;
}

void pll_reset(pll_t *p) {
	p->locked = 0;
	p->period = 0.0;
	p->phase = 0.0;
	p->last = 0.0;
	p->seated = 0.0;
}

static void seat(pll_t *p, double t) {
	p->phase = t;
	p->last = t;
	p->seated = t;
	p->locked = 1;
}

void pll_seed(pll_t *p, double period) {
	p->period = period;
	p->locked = 0;
}

int pll_locked(const pll_t *p) { return p->locked; }
double pll_period(const pll_t *p) { return p->period; }

double pll_observe(pll_t *p, double t) {
	const pll_config_t *cfg = &p->config;
	if (p->period <= 0.0) return 0.0;
	if (!p->locked) {
		seat(p, t);
		return 0.0;
	}
	double n = round_half_away((t - p->phase) / p->period);
	double predicted = p->phase + n * p->period;
	double error = t - predicted;
	if (fabs(error) > cfg->capture * p->period) {
		seat(p, t);
		return error;
	}
	double dt = t - p->last;
	double cycles = round_half_away(dt / p->period);
	if (cycles < 1.0) cycles = 1.0;
	double tc = (t - p->seated) / PLL_ACQUISITION;
	if (cfg->tc < tc) tc = cfg->tc;
	double alpha = tc > 0.0 ? 1.0 - exp(-dt / tc) : 1.0;
	double beta = alpha * alpha / (2.0 - alpha);   // a damping ratio of 0.7
	p->phase = predicted + alpha * error;
	p->period += beta * error / cycles;
	p->last = t;
	return error;
}

double pll_next(const pll_t *p, double after) {
	if (!p->locked) return after;
	double n = ceil((after - p->phase) / p->period);
	return p->phase + n * p->period;
}
