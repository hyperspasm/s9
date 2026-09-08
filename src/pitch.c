#include "pitch.h"

#include <math.h>

// -- estimation

static pitch_estimate_t refine(const float *nacf, uint32_t n, uint32_t p) {
	pitch_estimate_t e;
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

pitch_estimate_t pitch_estimate(const float *nacf, uint32_t n, uint32_t lag_min, uint32_t lag_max, float tolerance) {
	pitch_estimate_t none = { 0.0f, 0.0f };
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

struct pitch_conditioner {
	pitch_conditioner_config_t config;
	int valid;
	uint32_t hold;       // unpitched frames since the last accepted estimate
	int tracking;        // smoothed and reported hold a frequency
	double smoothed;     // in cents
	double reported;     // in cents
};

size_t pitch_conditioner_alloc_size(void) {
	return sizeof(pitch_conditioner_t);
}

void pitch_conditioner_init(pitch_conditioner_t *pc, const pitch_conditioner_config_t *config) {
	pc->config = *config;
	pitch_conditioner_reset(pc);
}

void pitch_conditioner_set_config(pitch_conditioner_t *pc, const pitch_conditioner_config_t *config) {
	pc->config = *config;
}

void pitch_conditioner_reset(pitch_conditioner_t *pc) {
	pc->valid = 0;
	pc->hold = 0;
	pc->tracking = 0;
	pc->smoothed = 0.0;
	pc->reported = 0.0;
}

int pitch_conditioner_update(pitch_conditioner_t *pc, float period, float clarity, float rms, float sample_rate, float *frequency) {
	const pitch_conditioner_config_t *cfg = &pc->config;
	double freq = period > 0.0f ? (double)sample_rate / (double)period : 0.0;
	int accepted = rms >= cfg->gate_rms && freq > 0.0 &&
	               ((!pc->valid && clarity >= cfg->clarity_on) || (pc->valid && clarity >= cfg->clarity_off));

	if (!accepted) {
		if (pc->valid && ++pc->hold > cfg->hold_frames) pc->valid = 0;
	} else {
		double c = 1200.0 * log2(freq);
		if (pc->valid) {
			double d = c - pc->smoothed;
			if (fabs(d - 1200.0) <= cfg->octave_tolerance_cents) c -= 1200.0;
			else if (fabs(d + 1200.0) <= cfg->octave_tolerance_cents) c += 1200.0;
			pc->smoothed += cfg->smoothing * (c - pc->smoothed);
		} else {
			pc->smoothed = c;
			pc->reported = c;
			pc->valid = 1;
			pc->tracking = 1;
		}
		pc->hold = 0;
		if (fabs(pc->smoothed - pc->reported) > cfg->deadband_cents) pc->reported = pc->smoothed;
	}

	if (frequency) *frequency = pc->tracking ? (float)pow(2.0, pc->reported / 1200.0) : 0.0f;
	return pc->valid;
}
