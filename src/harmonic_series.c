#include "harmonic_series.h"

#include <math.h>
#include <string.h>

// a series this fraction below f_min still counts: a fundamental at the edge is not a coin toss
#define F_MIN_MARGIN 0.02

// a partial: a prominent peak
typedef struct {
	double freq, level, pw;    // Hz, dB, and the power that level is
	long k;                    // the multiple it lies on, of the series being examined
	uint8_t strong;            // within strong_db of the strongest
	uint8_t usable;            // within fit_floor_db of the strongest
	uint8_t on;                // on the series being examined
} partial_t;

// the scratch: the partials, then a byte per possible multiple
static uint32_t partials_max(uint32_t bins) { return bins / 2 + 1; }
static uint32_t multiples_max(uint32_t bins) { return 2 * bins + 2; }

size_t harmonic_series_scratch_size(uint32_t bins) {
	return sizeof(partial_t) * partials_max(bins) + multiples_max(bins);
}

// the prominent peaks of the spectrum within the range examined
static uint32_t find_partials(const float *db, uint32_t bins, double delta_f, const harmonic_window_t *w,
                              const harmonic_series_config_t *cfg, partial_t *out, uint32_t max) {
	double top = -INFINITY;
	for (uint32_t p = 1; p + 1 < bins; p++)
		if (db[p] > top && db[p] > db[p - 1] && db[p] >= db[p + 1]) top = db[p];
	double lowest = cfg->f_min * 0.9, highest = (bins - 3) * delta_f;
	uint32_t count = 0;
	for (uint32_t p = 1; p + 1 < bins && count < max; p++) {
		if (!(db[p] > db[p - 1] && db[p] >= db[p + 1] && db[p] > top + cfg->peak_floor_db)) continue;
		double floor = harmonic_floor_db(db, bins, p, w->reach, cfg->shoulder);
		if (!(db[p] > floor + cfg->prominence_db)) continue;
		double freq, level;
		harmonic_refine(db, p, delta_f, w, &freq, &level);
		if (freq < lowest || freq > highest) continue;
		out[count++] = (partial_t){ freq, level, pow(10.0, level / 10.0), 0, 0, 0, 0 };
	}
	return count;
}

// mark which partials lie within tol_bins of a multiple of c, and the multiple
static void on_series(partial_t *ps, uint32_t count, double c, double delta_f, double tol_bins) {
	for (uint32_t i = 0; i < count; i++) {
		ps[i].k = lrint(ps[i].freq / c);
		ps[i].on = ps[i].k >= 1 && fabs(ps[i].freq - ps[i].k * c) < tol_bins * delta_f;
	}
}

static int all_strong_on(const partial_t *ps, uint32_t count) {
	for (uint32_t i = 0; i < count; i++)
		if (ps[i].strong && !ps[i].on) return 0;
	return 1;
}

// the least squares of f_k = k c over the usable partials on the series, weighted by power; c when there are none
static double fit(const partial_t *ps, uint32_t count, double c) {
	double num = 0.0, den = 0.0;
	for (uint32_t i = 0; i < count; i++) {
		if (!ps[i].on || !ps[i].usable) continue;
		num += ps[i].pw * ps[i].k * ps[i].freq;
		den += ps[i].pw * ps[i].k * ps[i].k;
	}
	return den > 0.0 ? num / den : c;
}

// The distinct multiples the partials on the series lie on: how many, the lowest and
// the highest, and, when divisor is above 1, how many are not multiples of it and how
// many slots between the lowest and highest are not. seen is scratch of seen_max bytes.
static void multiples(const partial_t *ps, uint32_t count, uint8_t *seen, uint32_t seen_max, long divisor,
                      uint32_t *distinct, long *lowest, long *highest, uint32_t *extra, uint32_t *extra_slots) {
	memset(seen, 0, seen_max);
	*distinct = 0; *lowest = -1; *highest = -1; *extra = 0; *extra_slots = 0;
	for (uint32_t i = 0; i < count; i++) {
		if (!ps[i].on || ps[i].k < 0 || (uint32_t)ps[i].k >= seen_max || seen[ps[i].k]) continue;
		seen[ps[i].k] = 1;
		(*distinct)++;
		if (*lowest < 0 || ps[i].k < *lowest) *lowest = ps[i].k;
		if (ps[i].k > *highest) *highest = ps[i].k;
		if (divisor > 1 && ps[i].k % divisor != 0) (*extra)++;
	}
	if (divisor > 1 && *lowest >= 0)
		for (long j = *lowest; j <= *highest; j++)
			if (j % divisor != 0) (*extra_slots)++;
}

harmonic_series_t harmonic_series_estimate(const float *db, uint32_t bins, double delta_f, const harmonic_window_t *w,
                                           const harmonic_series_config_t *cfg, void *scratch) {
	harmonic_series_t none = { 0.0f, 0.0f, 0, 0.0f };
	if (bins < 3) return none;
	partial_t *ps = scratch;
	uint8_t *seen = (uint8_t *)(ps + partials_max(bins));
	uint32_t seen_max = multiples_max(bins);
	uint32_t count = find_partials(db, bins, delta_f, w, cfg, ps, partials_max(bins));
	if (!count) return none;

	double top = -INFINITY;
	for (uint32_t i = 0; i < count; i++)
		if (ps[i].level > top) top = ps[i].level;
	for (uint32_t i = 0; i < count; i++) {
		ps[i].strong = ps[i].level > top - cfg->strong_db;
		ps[i].usable = ps[i].level > top - cfg->fit_floor_db;
	}

	// stage one: the largest frequency of which every strong partial is a multiple
	double f_low = INFINITY;
	for (uint32_t i = 0; i < count; i++)
		if (ps[i].strong && ps[i].freq < f_low) f_low = ps[i].freq;
	double lowest = cfg->f_min * (1.0 - F_MIN_MARGIN);
	double c0 = 0.0;
	int found = 0;
	for (uint32_t k = 1; k <= cfg->divisors; k++) {
		double c = f_low / k;
		if (c < lowest) break;
		on_series(ps, count, c, delta_f, cfg->coarse_bins);
		if (all_strong_on(ps, count)) {
			if (c > cfg->f_max) return none;
			c0 = c;
			found = 1;
			break;
		}
	}
	if (!found) {
		// no series holds every strong partial: the one holding the most of their power, of
		// those whose partials fill enough of their slots
		double held_best = -1.0;
		for (uint32_t k = 1; k <= cfg->divisors; k++) {
			double c = f_low / k;
			if (c < lowest) break;
			if (c > cfg->f_max) continue;
			on_series(ps, count, c, delta_f, cfg->coarse_bins);
			uint32_t distinct, extra, slots;
			long lo, hi;
			multiples(ps, count, seen, seen_max, 1, &distinct, &lo, &hi, &extra, &slots);
			if (!distinct || (double)distinct / (double)(hi - lo + 1) < cfg->sparse) continue;
			double held = 0.0;
			for (uint32_t i = 0; i < count; i++)
				if (ps[i].on && ps[i].strong) held += ps[i].pw;
			if (held > held_best) { held_best = held; c0 = c; found = 1; }
		}
		if (!found) return none;
	}
	on_series(ps, count, c0, delta_f, cfg->coarse_bins);
	c0 = fit(ps, count, c0);
	on_series(ps, count, c0, delta_f, cfg->fine_bins);
	c0 = fit(ps, count, c0);

	// stage two: the deepest sub-multiple the weaker partials support
	double chosen = c0;
	for (uint32_t divisor = 2; divisor <= cfg->divisors; divisor++) {
		double c = c0 / divisor;
		if (c < lowest) break;
		on_series(ps, count, c, delta_f, cfg->fine_bins);
		uint32_t distinct, extra, slots;
		long lo, hi;
		multiples(ps, count, seen, seen_max, (long)divisor, &distinct, &lo, &hi, &extra, &slots);
		if (!distinct) continue;
		double power = 0.0, extra_power = 0.0;
		for (uint32_t i = 0; i < count; i++) {
			if (!ps[i].on) continue;
			power += ps[i].pw;
			if (ps[i].k % (long)divisor != 0) extra_power += ps[i].pw;
		}
		if (extra >= cfg->min_extra && slots > 0 && (double)extra / (double)slots > cfg->completeness
			&& extra_power > cfg->extra_power * power)
			chosen = c;
	}

	// the fine value: weighted by power, then also by closeness to the series
	on_series(ps, count, chosen, delta_f, cfg->fine_bins);
	double f0 = fit(ps, count, chosen);
	double num = 0.0, den = 0.0;
	for (int pass = 0; pass < 3; pass++) {
		num = 0.0; den = 0.0;
		for (uint32_t i = 0; i < count; i++) {
			if (!ps[i].on || !ps[i].usable) continue;
			double weight = ps[i].pw;
			if (pass > 0) {
				double dev = (ps[i].freq - ps[i].k * f0) / delta_f;
				weight /= 1.0 + (dev / cfg->stray_bins) * (dev / cfg->stray_bins);
			}
			num += weight * ps[i].k * ps[i].freq;
			den += weight * ps[i].k * ps[i].k;
		}
		if (den > 0.0) f0 = num / den;
	}

	// harmonicity, count, and the uncertainty of the fit
	double total = 0.0;
	for (uint32_t b = 0; b < bins; b++)
		if (isfinite(db[b])) total += pow(10.0, db[b] / 10.0);
	double held = 0.0;
	uint32_t on_count = 0, reach = w->reach;
	double sum_w = 0.0, sum_w2r2 = 0.0;
	for (uint32_t i = 0; i < count; i++) {
		if (!ps[i].on) continue;
		on_count++;
		long b = lrint(ps[i].freq / delta_f);
		long lo = b - (long)reach, hi = b + (long)reach;
		if (lo < 0) lo = 0;
		if (hi > (long)bins - 1) hi = (long)bins - 1;
		for (long j = lo; j <= hi; j++)
			if (isfinite(db[j])) held += pow(10.0, db[j] / 10.0);
		if (!ps[i].usable) continue;
		double dev = (ps[i].freq - ps[i].k * f0) / delta_f;
		double weight = ps[i].pw / (1.0 + (dev / cfg->stray_bins) * (dev / cfg->stray_bins));
		double wk = weight * ps[i].k * ps[i].k;   // the weight of the mean of f_k / k
		double r = ps[i].freq / ps[i].k - f0;
		sum_w += wk;
		sum_w2r2 += wk * wk * r * r;
	}
	double harmonicity = total > 0.0 ? held / total : 0.0;
	if (harmonicity > 1.0) harmonicity = 1.0;
	double se = sum_w > 0.0 ? sqrt(sum_w2r2) / sum_w : 0.0;
	return (harmonic_series_t){ (float)f0, (float)harmonicity, on_count, (float)(1200.0 / log(2.0) * se / f0) };
}
