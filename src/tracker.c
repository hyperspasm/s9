#include "tracker.h"
#include "s9.h"
#include "fft.h"
#include "window_function.h"
#include "harmonic_readout.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HISTORY 32768              // samples kept: at least the longest frame and the longest search
#define STAMPS 128                 // hops of the lock's stamped periods kept for the warp
#define MAX_PEAKS 1024
#define MIN_LOG2 8                 // transforms from 2^8 to 2^15 points
#define MAX_LOG2 15
#define MAX_POINTS (1u << MAX_LOG2)
#define LIFTS 16                   // divisors a lift may take, bounded by the comb's span
#define MIDDLE_C_HZ 261.63

static const uint32_t sub_multiples[] = { 2, 4 };   // the octaves: a series at any other ratio below the lock is another source
static const uint32_t voices[] = { 2, 3, 4, 5, 6 }; // multiples tried as two voices sharing a comb

#define SUB_COUNT (sizeof sub_multiples / sizeof sub_multiples[0])
#define VOICE_COUNT (sizeof voices / sizeof voices[0])

typedef struct {
	double centre;     // the sample the period describes
	double period;     // 0 while unlocked
	uint32_t lock_id;  // which lock it belongs to
} stamp_t;

struct tracker {
	tracker_config_t cfg;
	double fs;
	uint32_t hop;
	// the audio
	float *hist;               // HISTORY samples, a ring
	uint64_t total;            // samples fed
	uint32_t pending;          // fed since the last hop
	float *seg;                // a linear copy of recent samples
	float *frame;              // the spectral frame, warped
	float *windows[MAX_LOG2 + 1];          // a window per frame length, made when first needed...
	harmonic_window_t models[MAX_LOG2 + 1];   // ...with the readout's model of it, for refining peaks
	float *bins, *bins2;       // (re, im) pairs
	float *time_a, *time_b;    // transform inputs and outputs
	double *corr;              // a correlation, or YIN's difference
	double *energy;            // prefix sums of squares
	float *db;                 // the spectrum's magnitudes
	double *phase;             // the warp's cumulative phase
	fft_t *ffts[MAX_LOG2 + 1];
	// the lock
	struct {
		double period;         // samples; 0 while unlocked
		double corr;
		double coast_s;
		double rate, slew;     // fractional change of the period per hop, smoothed, signed and in magnitude
		double centre;         // samples before the hop's end that the period describes
		double last_period;    // the period last held, and how long ago it was lost or slipped
		double lost_s;
		bool relocked;         // this hop's period came from a search
		double smooth, lag;    // the reported frequency, log2, and how far it trails the measured one, cents
		double candidates[TRACKER_AGREE_MAX];
		uint32_t n_candidates;
		uint32_t tau_min, tau_max;
		uint32_t id;           // counts locks, for the stamps
	} lock;
	stamp_t stamps[STAMPS];
	uint32_t n_stamps;
	// the fundamental
	struct {
		int g, m;              // the decision: the fundamental is the lock times g over m
		bool committed;
		double harmonicity;
		int pending_g, pending_m;
		bool has_pending;
		uint32_t pending_count;
		double low_s;
		double ratio;          // the fit's fundamental over the comb's, 0 for none...
		uint32_t fit_peaks;    // ...and the peaks it rests on
		double correction;
		double sub_seen[SUB_COUNT];
		double lift_seen[LIFTS];
	} fund;
	// the peaks of the last frame
	uint32_t n_peaks;
	double *pk_f, *pk_power, *pk_prom;
	uint8_t *evident, *on1, *on, *own;
	int32_t *k1, *k;
	// what is reported
	tracker_state_t state;
};

// -- helpers

static uint32_t samples(const tracker_t *t, float seconds) { return (uint32_t)lrint((double)seconds * t->fs); }

static uint32_t next_power_of_two(uint32_t n) {
	uint32_t p = 1u << MIN_LOG2;
	while (p < n) p <<= 1;
	return p;
}

static int log2_of(uint32_t p) {
	int l = 0;
	while ((1u << l) < p) l++;
	return l;
}

static fft_t *transform(tracker_t *t, uint32_t points) {
	int l = log2_of(points);
	if (l > MAX_LOG2) return NULL;
	if (!t->ffts[l]) t->ffts[l] = fft_new(points, FFT_REAL);
	return t->ffts[l];
}

static void parabola(double a, double b, double c, double *shift, double *peak) {
	double denom = a - 2.0 * b + c;
	*shift = fabs(denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0;
	*peak = b - 0.25 * (a - c) * *shift;
}

static uint32_t gcd(uint32_t a, uint32_t b) {
	while (b) { uint32_t r = a % b; a = b; b = r; }
	return a;
}

// the sample at an absolute position: silence before the stream and beyond what is kept
static inline float sample_at(const tracker_t *t, int64_t pos) {
	if (pos < 0 || pos >= (int64_t)t->total || pos + HISTORY < (int64_t)t->total) return 0.0f;
	return t->hist[pos & (HISTORY - 1)];
}

// the n samples ending at end into out
static void copy_segment(const tracker_t *t, int64_t end, uint32_t n, float *out) {
	for (uint32_t i = 0; i < n; i++) out[i] = sample_at(t, end - (int64_t)n + i);
}

// r[m] = sum_i a[i + m] b[i] for m < count, through a transform of size points, at least na + nb - 1
static int correlate_fft(tracker_t *t, const float *a, uint32_t na, const float *b, uint32_t nb, uint32_t points, double *r, uint32_t count) {
	fft_t *fft = transform(t, points);
	if (!fft) return 0;
	memset(t->time_a, 0, sizeof(float) * points);
	memcpy(t->time_a, a, sizeof(float) * na);
	fft_forward_real(fft, t->time_a, t->bins);
	memset(t->time_b, 0, sizeof(float) * points);
	memcpy(t->time_b, b, sizeof(float) * nb);
	fft_forward_real(fft, t->time_b, t->bins2);
	for (uint32_t i = 0; i <= points / 2; i++) {   // A times conj(B)
		float ar = t->bins[2 * i], ai = t->bins[2 * i + 1], br = t->bins2[2 * i], bi = t->bins2[2 * i + 1];
		t->bins[2 * i] = ar * br + ai * bi;
		t->bins[2 * i + 1] = ai * br - ar * bi;
	}
	fft_inverse_real(fft, t->bins, t->time_a);
	for (uint32_t m = 0; m < count; m++) r[m] = (double)t->time_a[m] / points;
	return 1;
}

// -- the lock

static double lock_frequency(const tracker_t *t) { return t->lock.period > 0.0 ? t->fs / t->lock.period : 0.0; }

static uint32_t window_len(const tracker_t *t) {
	const tracker_config_t *cfg = &t->cfg;
	double n = cfg->periods_in_window * t->lock.period;
	if (t->lock.slew > 1e-6) {
		double shorter = cfg->window_drift / t->lock.slew * t->hop;   // shorter while the pitch moves
		if (shorter < n) n = shorter;
	}
	if (n < 2.0 * t->lock.period) n = 2.0 * t->lock.period;
	double lo = samples(t, cfg->window_min_s), hi = samples(t, cfg->window_max_s);
	if (n < lo) n = lo;
	if (n > hi) n = hi;
	return (uint32_t)n;
}

// YIN's first dip under the threshold over a frame of n samples: the period, the dip's
// depth, and how many samples before the frame's end the period describes
static int search(tracker_t *t, const float *frame, uint32_t n, uint32_t tau_min, uint32_t tau_max, double *period, double *value, double *centre) {
	const tracker_config_t *cfg = &t->cfg;
	uint32_t w = n - tau_max > tau_max ? n - tau_max : tau_max;
	uint32_t points = next_power_of_two(2 * n + 1);
	double *acf = t->corr;
	if (!correlate_fft(t, frame, n, frame, w, points, acf, tau_max + 1)) return 0;   // sum x[i] x[i + tau] over i < w
	double *energy = t->energy;
	energy[0] = 0.0;
	for (uint32_t i = 0; i < n; i++) energy[i + 1] = energy[i] + (double)frame[i] * frame[i];
	double e0 = energy[w];
	if (e0 <= 1e-6 * energy[n]) return 0;   // the part compared is silent while the frame is not: an onset, no period yet
	double running = 0.0;
	double *cmndf = acf;   // in place: d over its running mean
	cmndf[0] = 1.0;
	for (uint32_t tau = 1; tau <= tau_max; tau++) {
		double d = e0 + (energy[w + tau] - energy[tau]) - 2.0 * acf[tau];
		running += d;
		cmndf[tau] = d * tau / (running > 0.0 ? running : 1e-12);
	}
	uint32_t tau = tau_min > 1 ? tau_min : 1;
	int found = -1;
	while (tau < tau_max) {
		if (cmndf[tau] < cfg->acquire_threshold) {
			while (tau + 1 < tau_max && cmndf[tau + 1] < cmndf[tau]) tau++;
			found = (int)tau;
			break;
		}
		tau++;
	}
	if (found < 0 || cmndf[found - 1] < cmndf[found]) return 0;   // no dip, or the flank of one at a shorter lag than the range
	double shift;
	parabola(cmndf[found - 1], cmndf[found], cmndf[found + 1], &shift, value);
	*period = found + shift;
	*centre = n - (w + *period) / 2.0;
	return 1;
}

static void lock_miss(tracker_t *t) {
	if (t->lock.coast_s == 0.0) { t->lock.last_period = t->lock.period; t->lock.lost_s = 0.0; }
	t->lock.coast_s += t->cfg.hop_s;
	t->lock.corr = 0.0;
	if (t->lock.coast_s > t->cfg.hold_s) {
		t->lock.period = 0.0;
		t->lock.n_candidates = 0;
	}
}

// the global search: a candidate period from each frame, short frames first; the lock is
// taken once consecutive candidates agree. Just after a lock was lost or slipped, the
// search stays near where it was: lags within a factor of three.
static void lock_acquire(tracker_t *t, int64_t end) {
	const tracker_config_t *cfg = &t->cfg;
	int quick = t->lock.last_period > 0.0 && t->lock.lost_s <= cfg->hold_s * 2.0;
	uint32_t tau_lo = t->lock.tau_min, tau_hi = t->lock.tau_max;
	if (quick) {
		uint32_t lo = (uint32_t)(t->lock.last_period / 3.0), hi = (uint32_t)(3.0 * t->lock.last_period);
		if (lo > tau_lo) tau_lo = lo;
		if (hi < tau_hi) tau_hi = hi;
	}
	double period = 0.0, value = 0.0, centre = 0.0;
	int found = 0;
	const float frames_s[2] = { cfg->acquire_short_s, cfg->acquire_long_s };
	for (int f = 0; f < 2 && !found; f++) {
		uint32_t n = samples(t, frames_s[f]);
		uint32_t tau_max = tau_hi < n / 3 ? tau_hi : n / 3;
		if (tau_max <= tau_lo + 2 || (int64_t)n > end) continue;
		copy_segment(t, end, n, t->seg);
		found = search(t, t->seg, n, tau_lo, tau_max, &period, &value, &centre);
	}
	if (!found) { t->lock.n_candidates = 0; return; }
	if (t->lock.period > 0.0 && fabs(period / t->lock.period - 1.0) <= cfg->search) {
		// the note coasted through is still there: the lock resumes rather than starting over
		t->lock.period = period;
		t->lock.corr = 1.0 - value;
		t->lock.centre = centre;
		t->lock.coast_s = 0.0;
		t->lock.n_candidates = 0;
		return;
	}
	uint32_t agree = cfg->acquire_agree < TRACKER_AGREE_MAX ? cfg->acquire_agree : TRACKER_AGREE_MAX;
	if (agree < 1) agree = 1;
	if (t->lock.n_candidates == agree) {   // keep the last agree - 1
		memmove(t->lock.candidates, t->lock.candidates + 1, sizeof(double) * (agree - 1));
		t->lock.n_candidates--;
	}
	t->lock.candidates[t->lock.n_candidates++] = period;
	if (t->lock.n_candidates < agree) return;
	// consecutive frames agreeing, each with the last, so a moving pitch can still lock
	for (uint32_t i = 1; i < agree; i++)
		if (fabs(t->lock.candidates[i] / t->lock.candidates[i - 1] - 1.0) > cfg->acquire_tolerance) return;
	t->lock.period = t->lock.candidates[agree - 1];
	t->lock.corr = 1.0 - value;
	t->lock.centre = centre;
	t->lock.coast_s = 0.0;
	t->lock.rate = t->lock.slew = 0.0;
	t->lock.n_candidates = 0;
	t->lock.relocked = true;
	t->lock.id++;
}

// the best lag in lo..hi of the last n samples before end against themselves, by
// normalized correlation refined by a parabola
static int correlate(tracker_t *t, int64_t end, uint32_t n, uint32_t lo, uint32_t hi, double *period, double *corr) {
	uint32_t count = hi - lo + 1;
	float *seg = t->seg;                       // the window and enough before it for every lag
	copy_segment(t, end, n + hi, seg);
	const float *ref = seg + hi;               // the last n samples
	uint32_t points = next_power_of_two(n + hi + 1);
	double *r = t->corr;
	if (!correlate_fft(t, seg, n + hi, ref, n, points, r, hi + 1)) return 0;   // r[m] = sum seg[i + m] ref[i]
	double *energy = t->energy;
	energy[0] = 0.0;
	for (uint32_t i = 0; i < n + hi; i++) energy[i + 1] = energy[i] + (double)seg[i] * seg[i];
	double e_ref = energy[hi + n] - energy[hi];
	int best = -1;
	double best_r = -2.0;
	for (uint32_t j = 0; j < count; j++) {   // the lag lo + j is at m = hi - lag
		uint32_t lag = lo + j, m = hi - lag;
		double e_lag = energy[m + n] - energy[m];
		double denom = e_ref * e_lag;
		double v = r[m] / sqrt(denom > 1e-30 ? denom : 1e-30);
		r[m] = v;
		if (v > best_r) { best_r = v; best = (int)j; }
	}
	if (best_r < t->cfg.track_min_corr || best == 0 || best == (int)count - 1) return 0;
	uint32_t lag = lo + (uint32_t)best, m = hi - lag;
	double shift;
	parabola(r[m + 1], r[m], r[m - 1], &shift, corr);   // the neighbours at lag - 1 and lag + 1 lie at m + 1 and m - 1
	*period = lag + shift;
	return 1;
}

static void lock_track(tracker_t *t, int64_t end) {
	const tracker_config_t *cfg = &t->cfg;
	uint32_t n = window_len(t);
	double p = t->lock.period;
	uint32_t lo = (uint32_t)(p * (1.0 - cfg->search)), hi = (uint32_t)ceil(p * (1.0 + cfg->search));
	if (lo < t->lock.tau_min) lo = t->lock.tau_min;
	if (hi > t->lock.tau_max) hi = t->lock.tau_max;
	if ((int64_t)(n + hi) > end || hi <= lo + 2) { lock_miss(t); return; }
	double period, value;
	if (!correlate(t, end, n, lo, hi, &period, &value)) { lock_miss(t); return; }
	double step = period / p - 1.0;
	t->lock.rate = 0.7 * t->lock.rate + 0.3 * step;
	t->lock.slew = 0.7 * t->lock.slew + 0.3 * fabs(step);
	t->lock.period = period;
	t->lock.corr = value;
	t->lock.centre = (n + period) / 2.0;
	t->lock.coast_s = 0.0;
}

// whether the waveform repeats at a divisor of the lock's period nearly as well as at the
// period itself, and well enough to be tracked there
static int lock_repeats_at(tracker_t *t, int64_t end, uint32_t divisor) {
	uint32_t n = window_len(t);
	uint32_t lag = (uint32_t)lrint(t->lock.period / divisor);
	if (lag < t->lock.tau_min || (int64_t)(n + lag) > end) return 0;
	copy_segment(t, end, n + lag, t->seg);
	const float *ref = t->seg + lag, *lagged = t->seg;
	double rr = 0.0, ee = 0.0, el = 0.0;
	for (uint32_t i = 0; i < n; i++) {
		rr += (double)ref[i] * lagged[i];
		ee += (double)ref[i] * ref[i];
		el += (double)lagged[i] * lagged[i];
	}
	double denom = ee * el;
	double r = rr / sqrt(denom > 1e-30 ? denom : 1e-30);
	double needed = t->lock.corr - t->cfg.lift_slack;
	if (needed < t->cfg.track_min_corr) needed = t->cfg.track_min_corr;
	return r >= needed;
}

// move the lock down to a divisor of its period, where the fundamental stage has found the
// series to be, if the waveform repeats there too
static int lock_retune(tracker_t *t, int64_t end, uint32_t divisor) {
	if (!lock_repeats_at(t, end, divisor)) return 0;
	t->lock.period /= divisor;
	t->lock.rate = t->lock.slew = 0.0;
	t->lock.smooth = log2(lock_frequency(t));
	t->lock.lag = 0.0;
	return 1;
}

// the reported frequency follows the measured one with a time constant set by its
// motion: long while it is still, short while it moves. The motion is the larger of the
// rate of change and how far the smoothed value has fallen behind, both smoothed so that
// jitter, which has no direction, counts little.
static void lock_smooth(tracker_t *t) {
	const tracker_config_t *cfg = &t->cfg;
	double f = log2(lock_frequency(t));
	if (t->lock.relocked || t->lock.smooth == 0.0) { t->lock.smooth = f; t->lock.lag = 0.0; return; }
	t->lock.lag = 0.7 * t->lock.lag + 0.3 * (f - t->lock.smooth) * 1200.0;
	double motion = fabs(t->lock.rate) * 1200.0 / log(2.0);
	if (fabs(t->lock.lag) > motion) motion = fabs(t->lock.lag);
	double tc_hops = cfg->smooth_max_s / cfg->hop_s;
	if (motion > 0.0 && cfg->smooth_drift_cents / motion < tc_hops) tc_hops = cfg->smooth_drift_cents / motion;
	if (tc_hops < 1.0) tc_hops = 1.0;
	t->lock.smooth += (f - t->lock.smooth) / tc_hops;
}

static void lock_update(tracker_t *t, int64_t end, int silent) {
	t->lock.relocked = false;
	if (t->lock.period <= 0.0 || t->lock.coast_s > 0.0) t->lock.lost_s += t->cfg.hop_s;
	if (silent) {
		if (t->lock.period > 0.0) lock_miss(t);
		else t->lock.n_candidates = 0;
	} else if (t->lock.period > 0.0) {
		lock_track(t, end);
		// coasting through something loud: a slip to a new note, or a burst; look for the new
		// note meanwhile, and take it as soon as consecutive frames agree on it
		if (t->lock.coast_s > 0.0) lock_acquire(t, end);
	} else {
		lock_acquire(t, end);
	}
	if (t->lock.period > 0.0) lock_smooth(t);
	else t->lock.smooth = 0.0;
}

// -- the fundamental

// the median of the finite values in db[lo..hi]; false with none
static int median_db(const float *db, long lo, long hi, double *out) {
	double v[HARMONIC_SHOULDER_MAX];
	int n = 0;
	for (long i = lo; i <= hi && n < HARMONIC_SHOULDER_MAX; i++) {
		if (!isfinite(db[i])) continue;
		int j = n++;
		while (j > 0 && v[j - 1] > db[i]) { v[j] = v[j - 1]; j--; }
		v[j] = db[i];
	}
	if (!n) return 0;
	*out = n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
	return 1;
}

// The floor beside bin p: on each side, the median of shoulder bins starting past the
// lobe; the lower side's. A side cut short by the spectrum's edge uses the bins it has,
// down to three: fewer make no floor, and the first bin alone would stand in for one.
// (harmonic_floor_db reads the same shoulders but takes any bin it can; the difference
// only shows at the edges.)
static double floor_db(const float *db, uint32_t bins, long p, uint32_t reach, uint32_t shoulder) {
	double floor = INFINITY, side;
	int any = 0;
	long hi = p - (long)reach - 1, lo = hi - (long)shoulder + 1;
	if (lo < 0) lo = 0;
	if (hi - lo + 1 >= 3 && median_db(db, lo, hi, &side)) { floor = side; any = 1; }
	lo = p + (long)reach + 1;
	hi = lo + (long)shoulder - 1;
	if (hi > (long)bins - 1) hi = (long)bins - 1;
	if (hi - lo + 1 >= 3 && median_db(db, lo, hi, &side) && side < floor) { floor = side; any = 1; }
	return any ? floor : -INFINITY;
}

static void fund_reset(tracker_t *t) {
	memset(&t->fund, 0, sizeof t->fund);
	t->fund.correction = 1.0;
}

// the spectral frame ending at end, warped by the lock's stamped history so that the
// pitch is constant across it: its length, and the pitch the samples now have
static uint32_t warped_frame(tracker_t *t, int64_t end, double f_lock, double *f_ref) {
	const tracker_config_t *cfg = &t->cfg;
	uint32_t n = next_power_of_two((uint32_t)(cfg->cycles_in_frame * t->fs / f_lock));
	if (n < cfg->frame_min) n = cfg->frame_min;
	if (n > cfg->frame_max) n = cfg->frame_max;
	int64_t start = end - (int64_t)n;
	// the stamps over the frame and a little before, in order
	double horizon = (double)start - t->fs * 0.05;
	double c[STAMPS + 1], p[STAMPS + 1];
	uint32_t count = 0;
	for (uint32_t i = 0; i < t->n_stamps; i++) {
		const stamp_t *s = &t->stamps[(t->n_stamps - 1 - i) & (STAMPS - 1)];   // newest first
		if (s->centre < horizon) break;
		if (s->period > 0.0) count++;
	}
	if (count < 2) {
		copy_segment(t, end, n, t->frame);
		*f_ref = f_lock;
		return n;
	}
	uint32_t j = 0;
	for (uint32_t i = 0; i < t->n_stamps; i++) {
		const stamp_t *s = &t->stamps[(t->n_stamps - 1 - i) & (STAMPS - 1)];
		if (s->centre < horizon) break;
		if (s->period > 0.0) { uint32_t at = count - 1 - j++; c[at] = s->centre; p[at] = s->period; }
	}
	if ((double)end > c[count - 1]) {   // carried on to the frame's end by the lock's rate of change
		c[count] = (double)end;
		p[count] = p[count - 1] * (1.0 + t->lock.rate * ((double)end - c[count - 1]) / t->hop);
		count++;
	}
	// the period at every sample of the frame, the phase it accumulates, and the samples
	// at uniform steps of phase
	double *phase = t->phase;
	uint32_t s = 0;
	double acc = 0.0;
	for (uint32_t i = 0; i < n; i++) {
		double ti = (double)(start + (int64_t)i);
		while (s + 1 < count && c[s + 1] < ti) s++;
		double pi;
		if (ti <= c[0]) pi = p[0];
		else if (s + 1 >= count) pi = p[count - 1];
		else pi = p[s] + (p[s + 1] - p[s]) * (ti - c[s]) / (c[s + 1] - c[s]);
		acc += 1.0 / pi;
		phase[i] = acc;
	}
	double first = phase[0];
	for (uint32_t i = 0; i < n; i++) phase[i] -= first;
	double cycles = phase[n - 1];
	uint32_t i = 0;
	for (uint32_t k = 0; k < n; k++) {
		double target = cycles * k / (n - 1);
		while (i + 1 < n - 1 && phase[i + 1] < target) i++;
		double span = phase[i + 1] - phase[i];
		double frac = span > 0.0 ? (target - phase[i]) / span : 0.0;
		if (frac < 0.0) frac = 0.0;
		if (frac > 1.0) frac = 1.0;
		int64_t pos = start + (int64_t)i;
		t->frame[k] = sample_at(t, pos) * (float)(1.0 - frac) + sample_at(t, pos + 1) * (float)frac;
	}
	*f_ref = cycles * t->fs / (n - 1);
	return n;
}

// the window for a frame length, summing to the length, and the readout's model of it
static const float *window_for(tracker_t *t, uint32_t n) {
	int l = log2_of(n);
	if (!t->windows[l]) {
		float *w = malloc(sizeof(float) * n);
		CHECK_ALLOC(w, return NULL);
		create_window_function(WINDOW_FUNCTION_BLACKMAN_HARRIS, n, 0, 0.0f, w);
		double sum = 0.0;
		for (uint32_t i = 0; i < n; i++) sum += w[i];
		for (uint32_t i = 0; i < n; i++) w[i] = (float)((double)w[i] * n / sum);
		harmonic_window_compute(&t->models[l], w, n);
		t->windows[l] = w;
	}
	return t->windows[l];
}

// the prominent peaks of the frame: frequency, power, prominence; each refined by a parabola
// through its neighbours less the window's bias in it
static void read_peaks(tracker_t *t, uint32_t n, double *bin_hz) {
	const tracker_config_t *cfg = &t->cfg;
	uint32_t bins = n / 2 + 1;
	const float *window = window_for(t, n);
	const harmonic_window_t *model = &t->models[log2_of(n)];
	if (!window) { t->n_peaks = 0; *bin_hz = t->fs / n; return; }
	for (uint32_t i = 0; i < n; i++) t->time_a[i] = t->frame[i] * window[i];
	fft_forward_real(transform(t, n), t->time_a, t->bins);
	harmonic_magnitude_db(t->bins, bins, n, t->db);
	*bin_hz = t->fs / n;
	float strongest = -INFINITY;
	for (uint32_t i = 1; i < bins; i++) if (t->db[i] > strongest) strongest = t->db[i];
	t->n_peaks = 0;
	for (uint32_t p = 1; p + 1 < bins && t->n_peaks < MAX_PEAKS; p++) {
		if (!(t->db[p] > t->db[p - 1] && t->db[p] >= t->db[p + 1] && t->db[p] > strongest - cfg->peak_floor_db)) continue;
		double floor = floor_db(t->db, bins, (long)p, cfg->lobe_reach, cfg->shoulder);
		double prom = t->db[p] - floor;
		if (!(prom >= cfg->prominence_db)) continue;
		double f, level;
		harmonic_refine(t->db, (long)p, *bin_hz, model, &f, &level);
		if (f < cfg->f_min * 0.5 || f > t->fs * 0.45) continue;
		t->pk_f[t->n_peaks] = f;
		t->pk_power[t->n_peaks] = pow(10.0, level / 10.0);
		t->pk_prom[t->n_peaks] = prom;
		t->n_peaks++;
	}
}

// which peaks lie on the harmonics of f0 up to f_cap, and their numbers
static void on_comb(const tracker_t *t, double f0, double bin_hz, double f_cap, int coarse, uint8_t *on, int32_t *k) {
	const tracker_config_t *cfg = &t->cfg;
	double b = cfg->inharmonicity_max * f0 / MIDDLE_C_HZ;
	for (uint32_t i = 0; i < t->n_peaks; i++) {
		double f = t->pk_f[i];
		double kk = floor(f / f0 + 0.5);
		double tol;
		if (coarse) {
			tol = cfg->coarse_fraction * f;
		} else {
			double stretch = sqrt(1.0 + b * kk * kk) - 1.0;   // a string's, but never past a fraction of the way to the next slot
			double cap = cfg->slot_fraction / (kk > 1.0 ? kk : 1.0);
			if (stretch > cap) stretch = cap;
			tol = cfg->on_comb_bins * bin_hz + (cfg->on_comb_fraction + stretch) * f;
		}
		k[i] = (int32_t)kk;
		on[i] = kk >= 1.0 && f <= f_cap && fabs(f - kk * f0) <= tol;
	}
}

// f0 refined by the lowest harmonics on its coarse comb, weighted by power
static double refined(tracker_t *t, double f0, double bin_hz, double f_cap) {
	on_comb(t, f0, bin_hz, f_cap, 1, t->own, t->k);
	double num = 0.0, den = 0.0;
	for (uint32_t i = 0; i < t->n_peaks; i++) {
		if (!t->own[i] || t->k[i] > (int32_t)t->cfg.refine_harmonics) continue;
		num += t->pk_power[i] * t->k[i] * t->pk_f[i];
		den += t->pk_power[i] * t->k[i] * t->k[i];
	}
	return den > 0.0 ? num / den : f0;
}

// Two voices sharing the comb: every harmonic found is a multiple of one of two coprime
// numbers above 1, one of them with at least two harmonics, and nothing else. The
// multiple of the louder voice, or 1.
static uint32_t two_voices(const tracker_t *t, const int32_t *k, const uint8_t *on) {
	uint32_t count = 0;
	for (uint32_t i = 0; i < t->n_peaks; i++) count += on[i] ? 1 : 0;
	if (count < 3) return 1;
	uint32_t best = 1;
	double best_power = 0.0;
	for (uint32_t ia = 0; ia < VOICE_COUNT; ia++) {
		for (uint32_t ib = ia + 1; ib < VOICE_COUNT; ib++) {
			uint32_t a = voices[ia], b = voices[ib];
			if (gcd(a, b) != 1) continue;
			uint32_t na = 0, nb = 0;
			double pa = 0.0, pb = 0.0;
			int all = 1;
			for (uint32_t i = 0; i < t->n_peaks && all; i++) {
				if (!on[i]) continue;
				int in_a = k[i] % (int32_t)a == 0, in_b = k[i] % (int32_t)b == 0;
				if (!in_a && !in_b) all = 0;
				if (in_a) { na++; pa += t->pk_power[i]; }
				if (in_b) { nb++; pb += t->pk_power[i]; }
			}
			if (!all || !na || !nb || (na < 2 && nb < 2)) continue;
			uint32_t louder = pa >= pb ? a : b;
			double power = pa >= pb ? pa : pb;
			if (power > best_power) { best = louder; best_power = power; }
		}
	}
	return best;
}

// The fundamental of the partials on the comb, over base: a fit of
// (f_k / k)^2 = f0^2 (1 + B k^2), a stretched string's law, weighted by power, so that f0
// is the lowest partial's frequency whatever the stretch. With fewer than three partials,
// or a fit that makes no sense, the plain weighted fit of f_k = k f0.
static double fit(const tracker_t *t, const uint8_t *on, const int32_t *k, double base, uint32_t *count) {
	uint32_t n = 0;
	double wsum = 0.0;
	for (uint32_t i = 0; i < t->n_peaks; i++) if (on[i]) { n++; wsum += t->pk_power[i]; }
	*count = n;
	if (n == 0 || wsum <= 0.0) return 0.0;
	if (n >= 3) {
		double um = 0.0, ym = 0.0;
		for (uint32_t i = 0; i < t->n_peaks; i++) {
			if (!on[i]) continue;
			double w = t->pk_power[i] / wsum, u = (double)k[i] * k[i], y = (t->pk_f[i] / k[i]) * (t->pk_f[i] / k[i]);
			um += w * u;
			ym += w * y;
		}
		double var = 0.0, cov = 0.0;
		for (uint32_t i = 0; i < t->n_peaks; i++) {
			if (!on[i]) continue;
			double w = t->pk_power[i] / wsum, u = (double)k[i] * k[i], y = (t->pk_f[i] / k[i]) * (t->pk_f[i] / k[i]);
			var += w * (u - um) * (u - um);
			cov += w * (u - um) * (y - ym);
		}
		if (var > 0.0) {
			double slope = cov / var, f0sq = ym - slope * um;
			if (f0sq > 0.0 && slope >= -1e-9 * f0sq) return sqrt(f0sq) / base;
		}
	}
	double num = 0.0, den = 0.0;
	for (uint32_t i = 0; i < t->n_peaks; i++) {
		if (!on[i]) continue;
		num += t->pk_power[i] * k[i] * t->pk_f[i];
		den += t->pk_power[i] * k[i] * k[i];
	}
	return num / den / base;
}

static double target(const tracker_t *t) {
	if (t->fund.ratio <= 0.0) return 1.0;
	double r = t->fund.ratio, lo = 1.0 - t->cfg.correction_max, hi = 1.0 + t->cfg.correction_max;
	return r < lo ? lo : r > hi ? hi : r;
}

// where a fresh decision's correction starts: at the fit when it rests on enough peaks
static double seed(const tracker_t *t) {
	return t->fund.fit_peaks >= t->cfg.seed_peaks ? target(t) : 1.0;
}

// an evaluation's decision, or none, into the committed one
static void observe(tracker_t *t, int has_decision, int g, int m) {
	const tracker_config_t *cfg = &t->cfg;
	double dt = cfg->hop_s;
	if (!has_decision || t->fund.harmonicity < cfg->harmonicity_off) {
		// nothing to go on: a committed decision holds, and lapses only after a while
		t->fund.has_pending = false;
		t->fund.pending_count = 0;
		if (t->fund.committed) {
			t->fund.low_s += dt;
			if (t->fund.low_s > cfg->lapse_s) { t->fund.committed = false; t->fund.g = t->fund.m = 0; t->fund.low_s = 0.0; }
		}
		return;
	}
	t->fund.low_s = 0.0;
	if (!t->fund.committed && t->fund.harmonicity < cfg->harmonicity_on) {
		t->fund.has_pending = false;
		t->fund.pending_count = 0;
		return;
	}
	int same_as_pending = t->fund.has_pending && t->fund.pending_g == g && t->fund.pending_m == m;
	if (t->fund.committed) {
		if (t->fund.g == g && t->fund.m == m) {
			// an evaluation for the committed decision sets a pending revision back a step, not to
			// nothing: a stray peak on the comb now and then must not keep a wrong decision in place
			if (t->fund.pending_count > 0) t->fund.pending_count--;
			if (t->fund.pending_count == 0) t->fund.has_pending = false;
			return;
		}
		if (same_as_pending) t->fund.pending_count++;
		else { t->fund.has_pending = true; t->fund.pending_g = g; t->fund.pending_m = m; t->fund.pending_count = 1; }
		// a sub-multiple's or a lift's evidence has already persisted; it replaces the lock's own
		// comb as quickly as a first decision commits
		int from_own = t->fund.g == 1 && t->fund.m == 1 && !(g == 1 && m == 1);
		uint32_t needed = from_own ? cfg->commit_evals : cfg->revise_evals;
		if (t->fund.pending_count >= needed) {
			t->fund.g = g; t->fund.m = m;
			t->fund.has_pending = false;
			t->fund.pending_count = 0;
			t->fund.correction = seed(t);
		}
		return;
	}
	if (same_as_pending) t->fund.pending_count++;
	else { t->fund.has_pending = true; t->fund.pending_g = g; t->fund.pending_m = m; t->fund.pending_count = 1; }
	if (t->fund.pending_count >= cfg->commit_evals) {
		t->fund.g = g; t->fund.m = m;
		t->fund.committed = true;
		t->fund.has_pending = false;
		t->fund.pending_count = 0;
		t->fund.correction = seed(t);
	}
}

static int lock_repeats_at(tracker_t *t, int64_t end, uint32_t divisor);

static void evaluate(tracker_t *t, int64_t end, double f_lock) {
	const tracker_config_t *cfg = &t->cfg;
	double f_ref, bin_hz;
	uint32_t n = warped_frame(t, end, f_lock, &f_ref);
	read_peaks(t, n, &bin_hz);
	if (t->n_peaks == 0) { t->fund.harmonicity = 0.0; observe(t, 0, 0, 0); return; }
	uint32_t np = t->n_peaks;
	double strongest = 0.0;
	for (uint32_t i = 0; i < np; i++) if (t->pk_power[i] > strongest) strongest = t->pk_power[i];
	double f_cap = cfg->comb_span * f_ref;
	// the peaks that count as evidence of a series: prominent, and within the balance of the strongest
	double balance = strongest * pow(10.0, -cfg->balance_db / 10.0);
	double total = 0.0;
	for (uint32_t i = 0; i < np; i++) {
		t->evident[i] = t->pk_prom[i] >= cfg->evidence_prominence_db && t->pk_power[i] >= balance;
		if (t->evident[i]) total += t->pk_power[i];
	}
	// the lock's comb, refined, and the divisor that lifts a lock sitting below the fundamental:
	// the greatest common divisor of the slots holding evidence
	double f1 = refined(t, f_ref, bin_hz, f_cap);
	on_comb(t, f1, bin_hz, f_cap, 0, t->on1, t->k1);
	double explained1 = 0.0;
	uint32_t g = 0, evident_on = 0;
	for (uint32_t i = 0; i < np; i++) {
		if (!t->on1[i]) continue;
		explained1 += t->pk_power[i];
		if (t->evident[i]) { evident_on++; g = gcd(g, (uint32_t)t->k1[i]); }
	}
	if (evident_on == 0 || g < 1 || g >= LIFTS) g = 1;
	// A lift, like a sub-multiple, must persist: the first frames of a note show its strongest
	// partial alone. And the waveform must repeat at the shorter period the lift claims: a lone
	// partial's number is otherwise no series, while a lock sitting on a multiple of a sine's period
	// is lifted by that one partial.
	int building = 0;
	for (uint32_t d = 2; d < LIFTS; d++)
		t->fund.lift_seen[d] += cfg->persistence_leak * ((d == g ? 1.0 : 0.0) - t->fund.lift_seen[d]);
	if (g > 1 && t->fund.lift_seen[g] < cfg->persistence) {
		building = t->fund.lift_seen[g] >= cfg->pending;
		g = 1;
	} else if (g > 1 && !lock_repeats_at(t, end, g)) {
		g = 1;
	}
	uint32_t best_g = g, best_m = 1;
	double best_f0 = f1 * g, best_explained = explained1;
	for (uint32_t si = 0; si < SUB_COUNT; si++) {
		uint32_t m = sub_multiples[si];
		double f0 = refined(t, f_ref / m, bin_hz, f_cap);
		int evidence = 0;
		if (f0 >= cfg->f_min * 0.98) {
			on_comb(t, f0, bin_hz, f_cap, 0, t->on, t->k);
			// the slots the lock's comb shares are judged by it; the sub-multiple's own slots are its evidence
			for (uint32_t i = 0; i < np; i++) {
				t->own[i] = t->on[i] && t->k[i] % (int32_t)m != 0;
				if (t->own[i] && t->evident[i] && t->pk_f[i] < cfg->evidence_span * f_ref) evidence = 1;
			}
		}
		// evidence must persist: a noise peak lands on an extra slot now and then, a sub-oscillator every time
		double *seen = &t->fund.sub_seen[si];
		*seen += cfg->persistence_leak * ((evidence ? 1.0 : 0.0) - *seen);
		if (evidence && *seen >= cfg->persistence) {
			double explained = 0.0;
			for (uint32_t i = 0; i < np; i++) if (t->on1[i] || t->own[i]) explained += t->pk_power[i];
			if (explained > best_explained * 1.0001) { best_g = 1; best_m = m; best_f0 = f0; best_explained = explained; }
		} else if (evidence && *seen >= cfg->pending) {
			building = 1;
		}
	}
	g = best_g;
	uint32_t m = best_m;
	double f0 = best_f0;
	if (g == 1 && m == 1 && building && !t->fund.committed) {
		// a sub-multiple or a lift may be about to win: the lock's own comb does not commit meanwhile
		t->fund.harmonicity = 0.0;
		return;
	}
	// the peaks on the chosen comb, over its own span: a lifted comb reaches further than the lock's
	on_comb(t, f0, bin_hz, cfg->comb_span * (f0 > f_ref ? f0 : f_ref), 0, t->on, t->k);
	uint32_t voice = two_voices(t, t->k, t->on);
	if (voice > 1) {
		g *= voice;
		f0 *= voice;
		on_comb(t, f0, bin_hz, cfg->comb_span * f0, 0, t->on, t->k);
	}
	double explained = 0.0;
	for (uint32_t i = 0; i < np; i++) if (t->on[i] && t->evident[i]) explained += t->pk_power[i];
	t->fund.harmonicity = total > 0.0 ? explained / total : 0.0;
	t->fund.ratio = fit(t, t->on, t->k, f_ref * g / m, &t->fund.fit_peaks);
	observe(t, 1, (int)g, (int)m);
}

// the reported fundamental: the lock over its ratio, corrected toward the fit
static double fund_value(tracker_t *t, double f_lock) {
	if (!t->fund.committed || f_lock <= 0.0) return 0.0;
	double base = f_lock * t->fund.g / t->fund.m;
	if (t->fund.ratio > 0.0) {
		double alpha = 1.0 - exp(-t->cfg.hop_s / t->cfg.correction_tc_s);
		t->fund.correction += alpha * (target(t) - t->fund.correction);
	}
	return base * t->fund.correction;
}

// -- the object

tracker_t *tracker_new(const tracker_config_t *config, uint32_t sample_rate) {
	CHECK_PARAMETER(sample_rate > 0, return NULL);
	tracker_t *t = calloc(1, sizeof(tracker_t));
	CHECK_ALLOC(t, return NULL);
	t->cfg = config ? *config : default_tracker_config;
	t->fs = sample_rate;
	t->hop = samples(t, t->cfg.hop_s);
	if (t->hop < 1) t->hop = 1;
	uint32_t bins = MAX_POINTS / 2 + 1;
	t->hist = calloc(HISTORY, sizeof(float));
	t->seg = calloc(HISTORY, sizeof(float));
	t->frame = calloc(MAX_POINTS, sizeof(float));
	t->bins = calloc(2 * bins, sizeof(float));
	t->bins2 = calloc(2 * bins, sizeof(float));
	t->time_a = calloc(MAX_POINTS, sizeof(float));
	t->time_b = calloc(MAX_POINTS, sizeof(float));
	t->corr = calloc(MAX_POINTS, sizeof(double));
	t->energy = calloc(HISTORY + 1, sizeof(double));
	t->db = calloc(bins, sizeof(float));
	t->phase = calloc(MAX_POINTS, sizeof(double));
	t->pk_f = calloc(MAX_PEAKS, sizeof(double));
	t->pk_power = calloc(MAX_PEAKS, sizeof(double));
	t->pk_prom = calloc(MAX_PEAKS, sizeof(double));
	t->evident = calloc(MAX_PEAKS, 1);
	t->on1 = calloc(MAX_PEAKS, 1);
	t->on = calloc(MAX_PEAKS, 1);
	t->own = calloc(MAX_PEAKS, 1);
	t->k1 = calloc(MAX_PEAKS, sizeof(int32_t));
	t->k = calloc(MAX_PEAKS, sizeof(int32_t));
	if (!t->hist || !t->seg || !t->frame || !t->bins || !t->bins2 || !t->time_a || !t->time_b || !t->corr
	    || !t->energy || !t->db || !t->phase || !t->pk_f || !t->pk_power || !t->pk_prom || !t->evident || !t->on1 || !t->on
	    || !t->own || !t->k1 || !t->k) {
		S9_LOG_ERROR("tracker: allocation failed");
		tracker_free(&t);
		return NULL;
	}
	tracker_reset(t);
	return t;
}

void tracker_free(tracker_t **tp) {
	if (!tp || !*tp) return;
	tracker_t *t = *tp;
	for (int l = 0; l <= MAX_LOG2; l++) { fft_free(&t->ffts[l]); free(t->windows[l]); }
	free(t->hist); free(t->seg); free(t->frame); free(t->bins); free(t->bins2);
	free(t->time_a); free(t->time_b); free(t->corr); free(t->energy); free(t->db); free(t->phase);
	free(t->pk_f); free(t->pk_power); free(t->pk_prom); free(t->evident); free(t->on1); free(t->on); free(t->own);
	free(t->k1); free(t->k);
	free(t);
	*tp = NULL;
}

void tracker_reset(tracker_t *t) {
	memset(t->hist, 0, sizeof(float) * HISTORY);
	t->total = 0;
	t->pending = 0;
	memset(&t->lock, 0, sizeof t->lock);
	t->lock.tau_min = (uint32_t)(t->fs / t->cfg.f_max);
	t->lock.tau_max = (uint32_t)(t->fs / t->cfg.f_min);
	t->n_stamps = 0;
	fund_reset(t);
	t->n_peaks = 0;
	memset(&t->state, 0, sizeof t->state);
	t->state.delay_s = t->cfg.window_min_s / 2.0f;
}

// one hop, ending at the newest sample
static void hop(tracker_t *t) {
	const tracker_config_t *cfg = &t->cfg;
	int64_t end = (int64_t)t->total;
	// silence: the last four hops quieter than the gate
	uint32_t recent = 4 * t->hop;
	if ((int64_t)recent > end) recent = (uint32_t)end;
	double sum = 0.0;
	for (uint32_t i = 0; i < recent; i++) { float v = sample_at(t, end - (int64_t)recent + i); sum += (double)v * v; }
	double rms = recent ? sqrt(sum / recent) : 0.0;
	int silent = rms < pow(10.0, cfg->gate_dbfs / 20.0);
	lock_update(t, end, silent);
	int locked = t->lock.period > 0.0;
	// the stamp: where the period stands; a search's can trail the last hop's, never before it
	double delay = locked ? t->lock.centre : samples(t, cfg->window_min_s) / 2.0;
	double centre = (double)end - delay;
	if (t->n_stamps > 0) {
		double prev = t->stamps[(t->n_stamps - 1) & (STAMPS - 1)].centre;
		if (centre < prev + 1.0) centre = prev + 1.0;
	}
	delay = (double)end - centre;
	t->stamps[t->n_stamps & (STAMPS - 1)] = (stamp_t){ centre, locked ? t->lock.period : 0.0, t->lock.id };
	t->n_stamps++;
	if (!locked || t->lock.relocked) fund_reset(t);
	if (locked && t->lock.corr > 0.0) {
		evaluate(t, end, lock_frequency(t));
		int g = t->fund.committed ? t->fund.g : 1;
		if (g > 1 && lock_retune(t, end, (uint32_t)g)) {
			// the lock moves down to the fundamental's series; its history and the decision follow
			for (uint32_t i = 0; i < t->n_stamps && i < STAMPS; i++) {
				stamp_t *s = &t->stamps[(t->n_stamps - 1 - i) & (STAMPS - 1)];
				if (s->lock_id == t->lock.id && s->period > 0.0) s->period /= g;
			}
			t->fund.g = 1;
		}
	}
	double f_lock = locked ? pow(2.0, t->lock.smooth) : 0.0;
	double f0 = fund_value(t, f_lock);
	t->state.locked = locked;
	t->state.lock_hz = (float)f_lock;
	t->state.lock_confidence = locked ? (float)t->lock.corr : 0.0f;
	t->state.committed = locked && t->fund.committed;
	t->state.fundamental_hz = (float)f0;
	t->state.harmonicity = (float)t->fund.harmonicity;
	t->state.delay_s = (float)(delay / t->fs);
}

uint32_t tracker_feed(tracker_t *t, const float *samples, uint32_t count) {
	uint32_t hops = 0;
	for (uint32_t i = 0; i < count; i++) {
		t->hist[t->total & (HISTORY - 1)] = samples[i];
		t->total++;
		if (++t->pending == t->hop) {
			t->pending = 0;
			hop(t);
			hops++;
		}
	}
	return hops;
}

tracker_state_t tracker_state(const tracker_t *t) { return t->state; }

uint32_t tracker_hop(const tracker_t *t) { return t->hop; }

void tracker_lock_internals(const tracker_t *t, double *period, double *rate, double *slew, uint32_t *window) {
	*period = t->lock.period;
	*rate = t->lock.rate;
	*slew = t->lock.slew;
	*window = t->lock.period > 0.0 ? window_len(t) : 0;
}

void tracker_fundamental_internals(const tracker_t *t, int *g, int *m, double *sub_seen, uint32_t *peaks) {
	*g = t->fund.committed ? t->fund.g : 0;
	*m = t->fund.committed ? t->fund.m : 0;
	for (uint32_t i = 0; i < SUB_COUNT; i++) sub_seen[i] = t->fund.sub_seen[i];
	*peaks = t->n_peaks;
}
