#include "period.h"

#include <math.h>

// -- estimation

static period_estimate_t refine(const float *nacf, uint32_t n, uint32_t p) {
	period_estimate_t e;
	double value;
	if (p < 1 || p + 1 >= n) {
		e.period = (float)p;
		value = nacf[p];
	} else {
		double y0 = nacf[p - 1], y1 = nacf[p], y2 = nacf[p + 1];
		double denom = y0 - 2.0 * y1 + y2;
		double delta = denom != 0.0 ? 0.5 * (y0 - y2) / denom : 0.0;
		e.period = (float)(p + delta);
		value = y1 - 0.25 * (y0 - y2) * delta;
	}
	e.clarity = (float)(value < 0.0 ? 0.0 : value > 1.0 ? 1.0 : value);
	return e;
}

// The maximum at or past lag_min of the positive region starting at *i,
// leaving *i past the region. Returns whether the region had one.
static int region_peak(const float *nacf, uint32_t *i, uint32_t lag_min, uint32_t lag_max, uint32_t *peak, float *value) {
	int found = 0;
	while (*i <= lag_max && nacf[*i] > 0.0f) {
		if (*i >= lag_min && (!found || nacf[*i] > *value)) {
			found = 1;
			*peak = *i;
			*value = nacf[*i];
		}
		(*i)++;
	}
	return found;
}

period_estimate_t period_estimate(const float *nacf, uint32_t n, uint32_t lag_min, uint32_t lag_max, float tolerance) {
	period_estimate_t none = { 0.0f, 0.0f };
	if (n < 2) return none;
	if (lag_max > n - 1) lag_max = n - 1;

	uint32_t start = 1;
	while (start <= lag_max && nacf[start] > 0.0f) start++;

	uint32_t peak;
	float value = 0.0f, best = 0.0f;
	int any = 0;
	for (uint32_t i = start; i <= lag_max;) {
		if (nacf[i] > 0.0f) {
			if (region_peak(nacf, &i, lag_min, lag_max, &peak, &value) && (!any || value > best)) {
				best = value;
				any = 1;
			}
		} else {
			i++;
		}
	}
	if (!any) return none;

	float threshold = best - tolerance;
	for (uint32_t i = start; i <= lag_max;) {
		if (nacf[i] > 0.0f) {
			if (region_peak(nacf, &i, lag_min, lag_max, &peak, &value) && value >= threshold)
				return refine(nacf, n, peak);
		} else {
			i++;
		}
	}
	return none;
}

// -- conditioning

struct period_conditioner {
	period_conditioner_config_t config;
	int valid;
	float hold_s;        // time without an accepted estimate
	float folded_s;      // time the octave guard has been folding
	int tracking;        // smoothed and reported hold a frequency
	double smoothed;     // in cents
	double reported;     // in cents
};

size_t period_conditioner_alloc_size(void) {
	return sizeof(period_conditioner_t);
}

void period_conditioner_init(period_conditioner_t *pc, const period_conditioner_config_t *config) {
	pc->config = *config;
	period_conditioner_reset(pc);
}

void period_conditioner_set_config(period_conditioner_t *pc, const period_conditioner_config_t *config) {
	pc->config = *config;
}

void period_conditioner_reset(period_conditioner_t *pc) {
	pc->valid = 0;
	pc->hold_s = 0.0f;
	pc->folded_s = 0.0f;
	pc->tracking = 0;
	pc->smoothed = 0.0;
	pc->reported = 0.0;
}

int period_conditioner_update(period_conditioner_t *pc, float period, float harmonicity, float rms, float sample_rate,
							 float dt_s, float *frequency) {
	const period_conditioner_config_t *cfg = &pc->config;
	double freq = period > 0.0f ? (double)sample_rate / (double)period : 0.0;
	int accepted = rms >= cfg->gate_rms && freq > 0.0 &&
	               ((!pc->valid && harmonicity >= cfg->harmonicity_on) || (pc->valid && harmonicity >= cfg->harmonicity_off));

	if (!accepted) {
		if (pc->valid) {
			pc->hold_s += dt_s;
			if (pc->hold_s > cfg->hold_s) pc->valid = 0;
		}
	} else {
		double c = 1200.0 * log2(freq);
		if (pc->valid) {
			double d = c - pc->smoothed, fold = 0.0;
			if (fabs(d - 1200.0) <= cfg->octave_tolerance_cents) fold = -1200.0;
			else if (fabs(d + 1200.0) <= cfg->octave_tolerance_cents) fold = 1200.0;
			if (fold != 0.0) {
				pc->folded_s += dt_s;
				if (pc->folded_s <= cfg->hold_s) c += fold;
			} else {
				pc->folded_s = 0.0f;
			}
			double alpha = cfg->smoothing_s > 0.0f ? 1.0 - exp(-(double)dt_s / cfg->smoothing_s) : 1.0;
			pc->smoothed += alpha * (c - pc->smoothed);
		} else {
			pc->smoothed = c;
			pc->reported = c;
			pc->valid = 1;
			pc->tracking = 1;
			pc->folded_s = 0.0f;
		}
		pc->hold_s = 0.0f;
		if (fabs(pc->smoothed - pc->reported) > cfg->deadband_cents) pc->reported = pc->smoothed;
	}

	if (frequency) *frequency = pc->tracking ? (float)pow(2.0, pc->reported / 1200.0) : 0.0f;
	return pc->valid;
}
