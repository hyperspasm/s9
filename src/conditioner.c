#include "conditioner.h"

#include <math.h>

struct conditioner {
	conditioner_config_t config;
	int valid;
	float hold_s;        // time without an accepted estimate
	float folded_s;      // time the octave guard has been folding
	int tracking;        // smoothed and reported hold a frequency
	double smoothed;     // in cents
	double reported;     // in cents
};

size_t conditioner_alloc_size(void) {
	return sizeof(conditioner_t);
}

void conditioner_init(conditioner_t *c, const conditioner_config_t *config) {
	c->config = *config;
	conditioner_reset(c);
}

void conditioner_set_config(conditioner_t *c, const conditioner_config_t *config) {
	c->config = *config;
}

void conditioner_reset(conditioner_t *c) {
	c->valid = 0;
	c->hold_s = 0.0f;
	c->folded_s = 0.0f;
	c->tracking = 0;
	c->smoothed = 0.0;
	c->reported = 0.0;
}

int conditioner_update(conditioner_t *c, float estimate_hz, float harmonicity, float rms, float dt_s, float *frequency) {
	const conditioner_config_t *cfg = &c->config;
	int accepted = rms >= cfg->gate_rms && estimate_hz > 0.0f &&
	               ((!c->valid && harmonicity >= cfg->harmonicity_on) || (c->valid && harmonicity >= cfg->harmonicity_off));

	if (!accepted) {
		if (c->valid) {
			c->hold_s += dt_s;
			if (c->hold_s > cfg->hold_s) c->valid = 0;
		}
	} else {
		double cents = 1200.0 * log2((double)estimate_hz);
		if (c->valid) {
			double d = cents - c->smoothed, fold = 0.0;
			if (fabs(d - 1200.0) <= cfg->octave_tolerance_cents) fold = -1200.0;
			else if (fabs(d + 1200.0) <= cfg->octave_tolerance_cents) fold = 1200.0;
			if (fold != 0.0) {
				c->folded_s += dt_s;
				if (c->folded_s <= cfg->hold_s) cents += fold;
			} else {
				c->folded_s = 0.0f;
			}
			if (fabs(cents - c->smoothed) > cfg->snap_cents) {
				c->smoothed = cents;
			} else {
				double alpha = cfg->smoothing_s > 0.0f ? 1.0 - exp(-(double)dt_s / cfg->smoothing_s) : 1.0;
				c->smoothed += alpha * (cents - c->smoothed);
			}
		} else {
			c->smoothed = cents;
			c->reported = cents;
			c->valid = 1;
			c->tracking = 1;
			c->folded_s = 0.0f;
		}
		c->hold_s = 0.0f;
		if (fabs(c->smoothed - c->reported) > cfg->deadband_cents) c->reported = c->smoothed;
	}

	if (frequency) *frequency = c->tracking ? (float)pow(2.0, c->reported / 1200.0) : 0.0f;
	return c->valid;
}
