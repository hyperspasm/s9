#include "constant_q.h"
#include "s9.h"

#include <fftw3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FLOOR 1e-20f   // a level below this is silence, -400 dB

typedef struct {
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
	float *taps;           // every readout's in turn, (re, im) pairs: conj(K) / n
	uint32_t tap_count;
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

constant_q_t *constant_q_new(const constant_q_config_t *config, const double *edges, uint32_t columns) {
	CHECK_PARAMETER(config->n >= 2 && config->n % 2 == 0 && columns > 0 && config->sample_rate > 0.0, return NULL);
	constant_q_t *cq = malloc(sizeof(constant_q_t));
	CHECK_ALLOC(cq, return NULL);
	memset(cq, 0, sizeof(*cq));
	cq->config = *config;
	cq->columns = columns;
	cq->column = calloc(columns, sizeof(column_t));
	CHECK_ALLOC(cq->column, constant_q_free(&cq); return NULL);

	// each column's window and readouts, then room for them all
	double bandwidth = window_function_bandwidth(config->window_function);
	uint32_t readouts = 0;
	for (uint32_t c = 0; c < columns; c++) {
		double lo = edges[c], hi = edges[c + 1];
		uint32_t length = window_length(config, bandwidth, sqrt(lo * hi));
		uint32_t count = readout_count(config, bandwidth, length, lo, hi);
		cq->column[c] = (column_t){ .window = length, .first = readouts, .count = count };
		readouts += count;
	}
	cq->readout = calloc(readouts, sizeof(readout_t));
	CHECK_ALLOC(cq->readout, constant_q_free(&cq); return NULL);
	cq->readout_count = readouts;

	uint32_t n = config->n, bins = n / 2 + 1;
	fftwf_complex *in = fftwf_malloc(sizeof(fftwf_complex) * n);
	fftwf_complex *out = fftwf_malloc(sizeof(fftwf_complex) * n);
	float *w = malloc(sizeof(float) * n);
	fftwf_plan plan = (in && out && w) ? fftwf_plan_dft_1d((int)n, in, out, FFTW_FORWARD, FFTW_ESTIMATE) : NULL;
	int ok = plan != NULL;
	if (!ok) S9_LOG_ERROR("constant_q: could not set up the kernels' transform");

	double threshold = pow(10.0, (double)config->threshold_db / 20.0);
	uint32_t capacity = 0;
	for (uint32_t c = 0; ok && c < columns; c++) {
		const column_t *col = &cq->column[c];
		double lo = edges[c], hi = edges[c + 1];
		uint32_t length = col->window;
		create_window_function(config->window_function, length, 0, 0.0f, w);
		double sum = 0.0;
		for (uint32_t i = 0; i < length; i++) sum += w[i];
		double scale = 2.0 / sum;
		uint32_t start = n / 2 - length / 2;

		for (uint32_t r = 0; ok && r < col->count; r++) {
			double f = lo * pow(hi / lo, ((double)r + 0.5) / (double)col->count);

			// the window shifted to the readout's frequency, in the middle of the frame
			memset(in, 0, sizeof(fftwf_complex) * n);
			double step = 2.0 * M_PI * f / config->sample_rate;
			for (uint32_t i = 0; i < length; i++) {
				double theta = step * (double)(start + i);
				in[start + i][0] = (float)(w[i] * scale * cos(theta));
				in[start + i][1] = (float)(w[i] * scale * sin(theta));
			}
			fftwf_execute(plan);

			// the run of bins between the first and the last above the threshold
			double peak = 0.0;
			for (uint32_t k = 0; k < bins; k++) {
				double m = (double)out[k][0] * out[k][0] + (double)out[k][1] * out[k][1];
				if (m > peak) peak = m;
			}
			double cut = peak * threshold * threshold;
			uint32_t first = 0, last = bins - 1;
			while (first < last && (double)out[first][0] * out[first][0] + (double)out[first][1] * out[first][1] < cut) first++;
			while (last > first && (double)out[last][0] * out[last][0] + (double)out[last][1] * out[last][1] < cut) last--;
			uint32_t count = last - first + 1;
			if (reserve(cq, &capacity, cq->tap_count + count) != 0) { ok = 0; break; }
			float *taps = cq->taps + 2 * cq->tap_count;
			for (uint32_t j = 0; j < count; j++) {
				taps[2 * j] = out[first + j][0] / (float)n;
				taps[2 * j + 1] = -out[first + j][1] / (float)n;
			}
			cq->readout[col->first + r] = (readout_t){ .first = first, .count = count, .offset = cq->tap_count };
			cq->tap_count += count;
		}
	}

	if (plan) fftwf_destroy_plan(plan);
	fftwf_free(in);
	fftwf_free(out);
	free(w);
	if (!ok) constant_q_free(&cq);
	return cq;
}

void constant_q_free(constant_q_t **cq) {
	if (cq && *cq) {
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

void constant_q_readout(const constant_q_t *cq, const float *spectrum, float *out_db) {
	for (uint32_t c = 0; c < cq->columns; c++) {
		const column_t *col = &cq->column[c];
		float best = 0.0f;
		for (uint32_t r = 0; r < col->count; r++) {
			const readout_t *ro = &cq->readout[col->first + r];
			const float *x = spectrum + 2 * ro->first;
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
