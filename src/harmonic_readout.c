#include "harmonic_readout.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define OFFSET_MAX 0.5        // the tables' range of reported offsets
#define LOBE_END_DB -40.0     // the main lobe ends where the window's transform has fallen this far below its peak
#define LOBE_SCAN_STEP 0.05   // bins, when looking for that
#define LOBE_SCAN_MAX 16.0    // bins, when the transform never falls that far

void harmonic_magnitude_db(const float *spectrum, uint32_t bins, uint32_t n, float *db) {
	double scale = 2.0 / n;
	for (uint32_t k = 0; k < bins; k++) {
		double re = spectrum[2 * k], im = spectrum[2 * k + 1];
		double mag = sqrt(re * re + im * im) * scale;
		db[k] = mag > 0.0 ? (float)(20.0 * log10(mag)) : -INFINITY;
	}
}

void harmonic_power_db(const float *power, uint32_t bins, uint32_t n, float *db) {
	double scale = (2.0 / n) * (2.0 / n);
	for (uint32_t k = 0; k < bins; k++) {
		double p = power[k] * scale;
		db[k] = p > 0.0 ? (float)(10.0 * log10(p)) : -INFINITY;
	}
}

// (offset from the centre sample, peak value) of the parabola through three samples
static void parabola(double y0, double y1, double y2, double *d, double *peak) {
	double denom = y0 - 2.0 * y1 + y2;
	*d = denom != 0.0 ? 0.5 * (y0 - y2) / denom : 0.0;
	*peak = y1 - 0.25 * (y0 - y2) * *d;
}

// the window's transform magnitude in dB relative to its peak, delta bins from its centre
static double lobe_db(const float *w, uint32_t n, double delta) {
	double theta = -2.0 * M_PI * delta / n;
	double c = cos(theta), s = sin(theta);
	double re = 1.0, im = 0.0, sum_re = 0.0, sum_im = 0.0, sum_w = 0.0;
	for (uint32_t j = 0; j < n; j++) {
		if ((j & 255) == 0) {
			// restart the rotation from an exact value so its rounding does not accumulate
			re = cos(theta * j);
			im = sin(theta * j);
		}
		sum_re += w[j] * re;
		sum_im += w[j] * im;
		sum_w += w[j];
		double next = re * c - im * s;
		im = re * s + im * c;
		re = next;
	}
	return 20.0 * log10(hypot(sum_re, sum_im) / sum_w);
}

// linear interpolation of a table over increasing xp, held at the ends
static double interp(double x, const double *xp, const double *fp, uint32_t m) {
	if (x <= xp[0]) return fp[0];
	if (x >= xp[m - 1]) return fp[m - 1];
	uint32_t i = 0;
	while (i + 2 < m && xp[i + 1] <= x) i++;
	double t = (x - xp[i]) / (xp[i + 1] - xp[i]);
	return fp[i] + t * (fp[i + 1] - fp[i]);
}

void harmonic_window_compute(harmonic_window_t *w, const float *window, uint32_t n) {
	enum { SAMPLES = 4 * HARMONIC_CORRECTION_POINTS };
	double reported[SAMPLES], db_bias[SAMPLES], offset_bias[SAMPLES];
	for (int i = 0; i < SAMPLES; i++) {
		double truth = OFFSET_MAX * i / (SAMPLES - 1);
		double y0 = lobe_db(window, n, -1.0 - truth);
		double y1 = lobe_db(window, n, 0.0 - truth);
		double y2 = lobe_db(window, n, 1.0 - truth);
		double d, peak;
		parabola(y0, y1, y2, &d, &peak);
		reported[i] = d;
		db_bias[i] = peak;   // the true peak is 0 dB by construction
		offset_bias[i] = d - truth;
	}
	for (int g = 0; g < HARMONIC_CORRECTION_POINTS; g++) {
		double x = OFFSET_MAX * g / (HARMONIC_CORRECTION_POINTS - 1);
		w->db_bias[g] = interp(x, reported, db_bias, SAMPLES);
		w->offset_bias[g] = interp(x, reported, offset_bias, SAMPLES);
	}

	// where the lobe ends, plus the half bin a peak may be off, rounded up
	double x = LOBE_SCAN_STEP;
	while (x < LOBE_SCAN_MAX && lobe_db(window, n, x) > LOBE_END_DB) x += LOBE_SCAN_STEP;
	w->reach = (uint32_t)ceil(x + 0.5);
}

// a table value at a reported offset, between the uniformly spaced points
static double table_at(const double *table, double offset) {
	double pos = offset / OFFSET_MAX * (HARMONIC_CORRECTION_POINTS - 1);
	int i = (int)pos;
	if (i >= HARMONIC_CORRECTION_POINTS - 1) return table[HARMONIC_CORRECTION_POINTS - 1];
	double t = pos - i;
	return table[i] + t * (table[i + 1] - table[i]);
}

// the median of the finite values among db[lo..hi]; returns whether there were any
static int median_db(const float *db, long lo, long hi, double *median) {
	double values[HARMONIC_SHOULDER_MAX];
	uint32_t count = 0;
	for (long i = lo; i <= hi && count < HARMONIC_SHOULDER_MAX; i++) {
		if (!isfinite(db[i])) continue;
		// insertion into sorted order
		uint32_t j = count++;
		while (j > 0 && values[j - 1] > db[i]) {
			values[j] = values[j - 1];
			j--;
		}
		values[j] = db[i];
	}
	if (!count) return 0;
	*median = count % 2 ? values[count / 2] : 0.5 * (values[count / 2 - 1] + values[count / 2]);
	return 1;
}

// the floor around bin p: the lower of the two sides' shoulder medians, or -inf without any usable bin
static double floor_db(const float *db, uint32_t bins, long p, uint32_t reach, uint32_t shoulder) {
	double floor = INFINITY, side;
	int any = 0;
	long lo = p - (long)reach - (long)shoulder + 1, hi = p - (long)reach;
	if (lo < 0) lo = 0;
	if (hi >= 0 && lo <= hi && median_db(db, lo, hi, &side)) { floor = side; any = 1; }
	lo = p + (long)reach;
	hi = p + (long)reach + (long)shoulder - 1;
	if (hi > (long)bins - 1) hi = (long)bins - 1;
	if (lo <= hi && median_db(db, lo, hi, &side) && side < floor) { floor = side; any = 1; }
	return any ? floor : -INFINITY;
}

uint32_t harmonic_shoulder(double spacing_bins, uint32_t reach, uint32_t shoulder) {
	long fit = (long)(spacing_bins / 2.0) - (long)reach;
	if (fit < 1) return 1;
	return (uint32_t)fit < shoulder ? (uint32_t)fit : shoulder;
}

harmonic_readout_t harmonic_readout(const float *db, uint32_t bins, double delta_f, double centre_hz, uint32_t search,
									uint32_t shoulder, const harmonic_window_t *w) {
	harmonic_readout_t r = { 0, -INFINITY, 0.0f, 0.0f };
	if (bins < 3) return r;
	long last = (long)bins - 2;   // the highest bin a parabola can be centred on
	long centre = lrint(centre_hz / delta_f);
	if (centre < 1 || centre > last) return r;
	if (shoulder > HARMONIC_SHOULDER_MAX) shoulder = HARMONIC_SHOULDER_MAX;

	long lo = centre - (long)search, hi = centre + (long)search;
	if (lo < 1) lo = 1;
	if (hi > last) hi = last;
	long p = lo;
	for (long i = lo + 1; i <= hi; i++)
		if (db[i] > db[p]) p = i;

	double y0 = db[p - 1], y1 = db[p], y2 = db[p + 1];
	if (!(y1 > y0 && y1 >= y2)) return r;
	r.peak = 1;
	r.prominence = (float)(y1 - floor_db(db, bins, p, w->reach, shoulder));
	if (!isfinite(y0) || !isfinite(y1) || !isfinite(y2)) {
		r.frequency = (float)(p * delta_f);
		r.db = (float)y1;
		return r;
	}

	double d, peak;
	parabola(y0, y1, y2, &d, &peak);
	double a = fabs(d) < OFFSET_MAX ? fabs(d) : OFFSET_MAX;
	double off = d - copysign(table_at(w->offset_bias, a), d);
	r.frequency = (float)((p + off) * delta_f);
	r.db = (float)(peak - table_at(w->db_bias, a));
	return r;
}

int harmonic_gate_update(harmonic_gate_t *g, const harmonic_readout_t *r, float on_db, float off_db, float hold_s, float dt_s) {
	if (r->peak && r->prominence >= on_db) {
		g->open = 1;
		g->below_s = 0.0f;
	} else if (g->open) {
		if (r->peak && r->prominence >= off_db) {
			g->below_s = 0.0f;
		} else {
			g->below_s += dt_s;
			if (g->below_s > hold_s) g->open = 0;
		}
	}
	return g->open;
}

// the phase at the frame's centre of the component peaking at bin p: the
// bin's phase, at the frame's first sample, advanced over half the frame
static double centre_phase(const float *spectrum, uint32_t n, uint32_t p) {
	double c = (n - 1) / 2.0;
	return atan2(spectrum[2 * p + 1], spectrum[2 * p]) + 2.0 * M_PI * p * c / n;
}

float harmonic_phase(const float *spectrum, uint32_t n, uint32_t p_k, uint32_t p_1, uint32_t k) {
	double theta = centre_phase(spectrum, n, p_k) - k * centre_phase(spectrum, n, p_1);
	double deg = fmod(theta * 180.0 / M_PI, 360.0);
	if (deg < 0.0) deg += 360.0;
	if (deg > 180.0) deg -= 360.0;
	return (float)deg;
}

int harmonic_thd(const harmonic_readout_t *reads, const int *counted, uint32_t count, float *thd) {
	if (!count || !counted[0]) return -1;
	double power = 0.0;
	for (uint32_t k = 1; k < count; k++)
		if (counted[k]) power += pow(10.0, reads[k].db / 10.0);
	*thd = (float)(sqrt(power) / pow(10.0, reads[0].db / 20.0));
	return 0;
}
