#include "constant_q.h"
#include "s9.h"
#include "fft.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FLOOR 1e-20f   // a level below this is silence, -400 dB

// a transform of the middle of the frame, shared by the readouts whose windows it holds
typedef struct {
	uint32_t n;            // its length
	uint32_t offset;       // where its samples start in the frame
	float *in;             // n samples
	float *out;            // n/2 + 1 bins as (re, im) pairs
	fft_t *fft;
} level_t;

typedef struct {
	uint32_t level;    // the transform it reads
	uint32_t first;    // the bin of the first tap
	uint32_t count;    // taps
	uint32_t offset;   // the first tap's index into taps
} readout_t;

typedef struct {
	uint32_t window;   // the window length, frames
	uint32_t first;    // the column's first readout
	uint32_t count;    // its readouts
} column_t;

struct constant_q {
	constant_q_config_t config;
	uint32_t columns;
	column_t *column;
	readout_t *readout;    // every column's in turn
	uint32_t readout_count;
	float *taps;           // every readout's in turn, (re, im) pairs: conj(K) / its level's n
	uint32_t tap_count;
	level_t *level;        // longest first
	uint32_t levels;
};

// A column's window length: even, within [min_window, n]
static uint32_t window_length(const constant_q_config_t *c, double bandwidth, double f) {
	double semitones = pow(2.0, (double)c->resolution_semitones / 12.0) - 1.0;
	double length = bandwidth * c->sample_rate / (f * semitones);
	if (length < (double)c->min_window) length = (double)c->min_window;
	if (length > (double)c->n) length = (double)c->n;
	uint32_t even = 2 * (uint32_t)floor(length / 2.0 + 0.5);
	if (even < 2) even = 2;
	if (even > c->n) even = c->n;
	return even;
}

// a column's readouts: as many as keep them no farther apart than the ridge's width over readouts_per_ridge
static uint32_t readout_count(const constant_q_config_t *c, double bandwidth, uint32_t length, double lo, double hi) {
	double ridge = bandwidth * c->sample_rate / (double)length;
	double count = ceil((double)c->readouts_per_ridge * (hi - lo) / ridge);
	return count < 1.0 ? 1 : (uint32_t)count;
}

// the transform a window of the length is read through: the shortest power of two that holds it, or the frame
static uint32_t level_length(const constant_q_config_t *c, uint32_t length) {
	uint32_t p = FFT_MIN_LENGTH;
	while (p < length) p *= 2;
	return p <= c->n ? p : c->n;
}

// the level of that length, made if there is none yet; levels are kept longest first
static int level_index(constant_q_t *cq, uint32_t n) {
	for (uint32_t i = 0; i < cq->levels; i++)
		if (cq->level[i].n == n) return (int)i;
	level_t *grown = realloc(cq->level, sizeof(level_t) * (cq->levels + 1));
	CHECK_ALLOC(grown, return -1);
	cq->level = grown;
	uint32_t i = cq->levels;
	while (i > 0 && cq->level[i - 1].n < n) { cq->level[i] = cq->level[i - 1]; i--; }
	cq->level[i] = (level_t){ .n = n, .offset = cq->config.n / 2 - n / 2 };
	cq->levels++;
	return (int)i;
}

static int reserve(constant_q_t *cq, uint32_t *capacity, uint32_t needed) {
	if (needed <= *capacity) return 0;
	uint32_t grown = *capacity ? *capacity : 4096;
	while (grown < needed) grown *= 2;
	float *taps = realloc(cq->taps, sizeof(float) * 2 * grown);
	CHECK_ALLOC(taps, return -1);
	cq->taps = taps;
	*capacity = grown;
	return 0;
}

// the power of a bin of an (re, im) array
static double bin_power(const float *bins, uint32_t k) {
	return (double)bins[2 * k] * bins[2 * k] + (double)bins[2 * k + 1] * bins[2 * k + 1];
}

// the transform of one readout's kernel: the window shifted to f in the middle of a level's frame
static int build_kernel(constant_q_t *cq, const level_t *lv, float *in, float *out, fft_t *fft,
                        const float *w, uint32_t length, double scale, double f, uint32_t *capacity, readout_t *ro) {
	uint32_t n = lv->n, bins = n / 2 + 1, start = n / 2 - length / 2;
	memset(in, 0, sizeof(float) * 2 * n);
	double step = 2.0 * M_PI * f / cq->config.sample_rate;
	for (uint32_t i = 0; i < length; i++) {
		double theta = step * (double)(start + i);
		in[2 * (start + i)] = (float)(w[i] * scale * cos(theta));
		in[2 * (start + i) + 1] = (float)(w[i] * scale * sin(theta));
	}
	fft_forward_complex(fft, in, out);

	// the run of bins between the first and the last above the threshold
	double threshold = pow(10.0, (double)cq->config.threshold_db / 20.0);
	double peak = 0.0;
	for (uint32_t k = 0; k < bins; k++) {
		double m = bin_power(out, k);
		if (m > peak) peak = m;
	}
	double cut = peak * threshold * threshold;
	uint32_t first = 0, last = bins - 1;
	while (first < last && bin_power(out, first) < cut) first++;
	while (last > first && bin_power(out, last) < cut) last--;
	uint32_t count = last - first + 1;
	if (reserve(cq, capacity, cq->tap_count + count) != 0) return -1;
	float *taps = cq->taps + 2 * cq->tap_count;
	for (uint32_t j = 0; j < count; j++) {
		taps[2 * j] = out[2 * (first + j)] / (float)n;
		taps[2 * j + 1] = -out[2 * (first + j) + 1] / (float)n;
	}
	*ro = (readout_t){ .level = (uint32_t)(lv - cq->level), .first = first, .count = count, .offset = cq->tap_count };
	cq->tap_count += count;
	return 0;
}

constant_q_t *constant_q_new(const constant_q_config_t *config, const double *edges, uint32_t columns) {
	CHECK_PARAMETER(config->n == fft_fast_length(config->n) && columns > 0 && config->sample_rate > 0.0, return NULL);
	constant_q_t *cq = malloc(sizeof(constant_q_t));
	CHECK_ALLOC(cq, return NULL);
	memset(cq, 0, sizeof(*cq));
	cq->config = *config;
	cq->columns = columns;
	cq->column = calloc(columns, sizeof(column_t));
	CHECK_ALLOC(cq->column, constant_q_free(&cq); return NULL);

	// each column's window, readouts and level, then room for the readouts
	double bandwidth = window_function_bandwidth(config->window_function);
	uint32_t readouts = 0;
	for (uint32_t c = 0; c < columns; c++) {
		double lo = edges[c], hi = edges[c + 1];
		uint32_t length = window_length(config, bandwidth, sqrt(lo * hi));
		uint32_t count = readout_count(config, bandwidth, length, lo, hi);
		cq->column[c] = (column_t){ .window = length, .first = readouts, .count = count };
		readouts += count;
		if (level_index(cq, level_length(config, length)) < 0) { constant_q_free(&cq); return NULL; }
	}
	cq->readout = calloc(readouts, sizeof(readout_t));
	CHECK_ALLOC(cq->readout, constant_q_free(&cq); return NULL);
	cq->readout_count = readouts;

	// the levels' transforms for the readouts, and complex ones for the kernels while they are built
	int ok = 1;
	uint32_t longest = cq->level[0].n;
	float *in = malloc(sizeof(float) * 2 * longest);
	float *out = malloc(sizeof(float) * 2 * longest);
	float *w = malloc(sizeof(float) * longest);
	fft_t **kernel_ffts = calloc(cq->levels, sizeof(fft_t *));
	ok = in && out && w && kernel_ffts;
	for (uint32_t i = 0; ok && i < cq->levels; i++) {
		level_t *lv = &cq->level[i];
		lv->in = malloc(sizeof(float) * lv->n);
		lv->out = malloc(sizeof(float) * 2 * (lv->n / 2 + 1));
		lv->fft = fft_new(lv->n, FFT_REAL);
		kernel_ffts[i] = fft_new(lv->n, FFT_COMPLEX);
		ok = lv->in && lv->out && lv->fft && kernel_ffts[i];
	}
	if (!ok) S9_LOG_ERROR("constant_q: could not set up the transforms");

	uint32_t capacity = 0;
	for (uint32_t c = 0; ok && c < columns; c++) {
		const column_t *col = &cq->column[c];
		double lo = edges[c], hi = edges[c + 1];
		uint32_t length = col->window;
		int li = level_index(cq, level_length(config, length));
		create_window_function(config->window_function, length, 0, 0.0f, w);
		double sum = 0.0;
		for (uint32_t i = 0; i < length; i++) sum += w[i];
		double scale = 2.0 / sum;
		for (uint32_t r = 0; ok && r < col->count; r++) {
			double f = lo * pow(hi / lo, ((double)r + 0.5) / (double)col->count);
			ok = build_kernel(cq, &cq->level[li], in, out, kernel_ffts[li], w, length, scale, f, &capacity, &cq->readout[col->first + r]) == 0;
		}
	}

	if (kernel_ffts) {
		for (uint32_t i = 0; i < cq->levels; i++) fft_free(&kernel_ffts[i]);
		free(kernel_ffts);
	}
	free(in);
	free(out);
	free(w);
	if (!ok) constant_q_free(&cq);
	return cq;
}

void constant_q_free(constant_q_t **cq) {
	if (cq && *cq) {
		for (uint32_t i = 0; i < (*cq)->levels; i++) {
			level_t *lv = &(*cq)->level[i];
			fft_free(&lv->fft);
			free(lv->in);
			free(lv->out);
		}
		free((*cq)->level);
		free((*cq)->column);
		free((*cq)->readout);
		free((*cq)->taps);
		free(*cq);
		*cq = NULL;
	}
}

uint32_t constant_q_columns(const constant_q_t *cq) { return cq->columns; }
uint32_t constant_q_window(const constant_q_t *cq, uint32_t column) { return column < cq->columns ? cq->column[column].window : 0; }
uint32_t constant_q_readouts(const constant_q_t *cq, uint32_t column) { return column < cq->columns ? cq->column[column].count : 0; }
uint32_t constant_q_taps(const constant_q_t *cq) { return cq->tap_count; }

void constant_q_readout(constant_q_t *cq, const float *frame, float *out_db) {
	for (uint32_t i = 0; i < cq->levels; i++) {
		level_t *lv = &cq->level[i];
		fft_forward_real(lv->fft, frame + lv->offset, lv->out);
	}
	for (uint32_t c = 0; c < cq->columns; c++) {
		const column_t *col = &cq->column[c];
		float best = 0.0f;
		for (uint32_t r = 0; r < col->count; r++) {
			const readout_t *ro = &cq->readout[col->first + r];
			const float *x = cq->level[ro->level].out + 2 * ro->first;
			const float *t = cq->taps + 2 * ro->offset;
			float re = 0.0f, im = 0.0f;
			for (uint32_t j = 0; j < ro->count; j++) {
				float xr = x[2 * j], xi = x[2 * j + 1], tr = t[2 * j], ti = t[2 * j + 1];
				re += xr * tr - xi * ti;
				im += xr * ti + xi * tr;
			}
			float power = re * re + im * im;
			if (power > best) best = power;
		}
		float magnitude = sqrtf(best);
		if (magnitude < FLOOR) magnitude = FLOOR;
		out_db[c] = 20.0f * log10f(magnitude);
	}
}
