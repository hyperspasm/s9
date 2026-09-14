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
	uint32_t window;   // the window length, frames
	uint32_t first;    // the bin of the first tap
	uint32_t count;    // taps
	uint32_t offset;   // the first tap's index into taps
} column_t;

struct constant_q {
	constant_q_config_t config;
	uint32_t columns;
	column_t *column;
	float *taps;         // every column's in turn, (re, im) pairs: conj(K) / n
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

constant_q_t *constant_q_new(const constant_q_config_t *config, const double *frequencies, uint32_t columns) {
	CHECK_PARAMETER(config->n >= 2 && config->n % 2 == 0 && columns > 0 && config->sample_rate > 0.0, return NULL);
	constant_q_t *cq = malloc(sizeof(constant_q_t));
	CHECK_ALLOC(cq, return NULL);
	memset(cq, 0, sizeof(*cq));
	cq->config = *config;
	cq->columns = columns;
	cq->column = calloc(columns, sizeof(column_t));
	CHECK_ALLOC(cq->column, constant_q_free(&cq); return NULL);

	uint32_t n = config->n, bins = n / 2 + 1;
	fftwf_complex *in = fftwf_malloc(sizeof(fftwf_complex) * n);
	fftwf_complex *out = fftwf_malloc(sizeof(fftwf_complex) * n);
	float *w = malloc(sizeof(float) * n);
	fftwf_plan plan = (in && out && w) ? fftwf_plan_dft_1d((int)n, in, out, FFTW_FORWARD, FFTW_ESTIMATE) : NULL;
	int ok = plan != NULL;
	if (!ok) S9_LOG_ERROR("constant_q: could not set up the kernels' transform");

	double bandwidth = window_function_bandwidth(config->window_function);
	double threshold = pow(10.0, (double)config->threshold_db / 20.0);
	uint32_t capacity = 0;
	for (uint32_t c = 0; ok && c < columns; c++) {
		double f = frequencies[c];
		uint32_t length = window_length(config, bandwidth, f);
		create_window_function(config->window_function, length, 0, 0.0f, w);
		double sum = 0.0;
		for (uint32_t i = 0; i < length; i++) sum += w[i];
		double scale = 2.0 / sum;
		uint32_t start = n / 2 - length / 2;

		// the window shifted to the column's frequency, in the middle of the frame
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
		cq->column[c] = (column_t){ .window = length, .first = first, .count = count, .offset = cq->tap_count };
		cq->tap_count += count;
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
		free((*cq)->taps);
		free(*cq);
		*cq = NULL;
	}
}

uint32_t constant_q_columns(const constant_q_t *cq) { return cq->columns; }
uint32_t constant_q_window(const constant_q_t *cq, uint32_t column) { return column < cq->columns ? cq->column[column].window : 0; }
uint32_t constant_q_taps(const constant_q_t *cq) { return cq->tap_count; }

void constant_q_readout(const constant_q_t *cq, const float *spectrum, float *out_db) {
	for (uint32_t c = 0; c < cq->columns; c++) {
		const column_t *col = &cq->column[c];
		const float *x = spectrum + 2 * col->first;
		const float *t = cq->taps + 2 * col->offset;
		float re = 0.0f, im = 0.0f;
		for (uint32_t j = 0; j < col->count; j++) {
			float xr = x[2 * j], xi = x[2 * j + 1], tr = t[2 * j], ti = t[2 * j + 1];
			re += xr * tr - xi * ti;
			im += xr * ti + xi * tr;
		}
		float magnitude = sqrtf(re * re + im * im);
		if (magnitude < FLOOR) magnitude = FLOOR;
		out_db[c] = 20.0f * log10f(magnitude);
	}
}
