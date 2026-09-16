#include "harmonic_reader.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct harmonic_reader {
	harmonic_reader_config_t config;
	harmonic_window_t window;
	uint32_t window_n;                  // the transform length the window model is for; 0 before any
	harmonic_gate_t gates[HARMONIC_MAX];
	struct { float re, im; } phases[HARMONIC_MAX];   // each harmonic's relative phase as a unit vector, averaged
	harmonic_reading_t readings[HARMONIC_MAX];
	uint32_t count;
	float thd;
	int has_thd;
	double last_f0;                     // the frame before's fundamental, 0 without
	int restarted;
};

size_t harmonic_reader_alloc_size(void) {
	return sizeof(harmonic_reader_t);
}

void harmonic_reader_init(harmonic_reader_t *r, const harmonic_reader_config_t *config) {
	memset(r, 0, sizeof(*r));
	harmonic_reader_set_config(r, config);
}

void harmonic_reader_set_config(harmonic_reader_t *r, const harmonic_reader_config_t *config) {
	r->config = *config;
	if (r->config.harmonics < 1) r->config.harmonics = 1;
	if (r->config.harmonics > HARMONIC_MAX) r->config.harmonics = HARMONIC_MAX;
}

void harmonic_reader_set_window(harmonic_reader_t *r, const float *window, uint32_t n) {
	harmonic_window_compute(&r->window, window, n);
	r->window_n = n;
}

// the gates close and the phases start over; the readings stay until the next frame
static void start_over(harmonic_reader_t *r) {
	memset(r->gates, 0, sizeof(r->gates));
	memset(r->phases, 0, sizeof(r->phases));
}

void harmonic_reader_reset(harmonic_reader_t *r) {
	start_over(r);
	r->count = 0;
	r->has_thd = 0;
	r->last_f0 = 0.0;
	r->restarted = 0;
}

void harmonic_reader_update(harmonic_reader_t *r, const float *db, const float *spectrum, uint32_t n, double delta_f,
							double f0, int locked, float alpha, float dt_s) {
	const harmonic_reader_config_t *cfg = &r->config;

	// a new note: everything read so far was of another
	r->restarted = f0 > 0.0 && r->last_f0 > 0.0 && fabs(1200.0 * log2(f0 / r->last_f0)) > cfg->note_jump_cents;
	if (r->restarted) start_over(r);
	r->last_f0 = f0;

	r->count = 0;
	r->has_thd = 0;
	if (!locked || f0 <= 0.0 || r->window_n != n) {
		start_over(r);
		return;
	}

	// the harmonics below the last bin, each read and gated
	uint32_t bins = n / 2 + 1;
	double last_hz = (bins - 1) * delta_f;
	uint32_t shoulder = harmonic_shoulder(f0 / delta_f, r->window.reach, cfg->shoulder_bins);
	for (uint32_t k = 1; k <= cfg->harmonics; k++) {
		double fk = k * f0;
		if (fk > last_hz) break;
		uint32_t search = (uint32_t)ceil(fk * cfg->search_ratio / delta_f);
		if (search < cfg->search_min_bins) search = cfg->search_min_bins;
		harmonic_reading_t *reading = &r->readings[k - 1];
		reading->readout = harmonic_readout(db, bins, delta_f, fk, search, shoulder, &r->window);
		reading->shown = harmonic_gate_update(&r->gates[k - 1], &reading->readout, cfg->prominence_db,
											  cfg->prominence_db - cfg->hysteresis_db, cfg->hold_s, dt_s);
		r->count = k;
	}

	// each harmonic's phase relative to the fundamental's, averaged as a unit
	// vector by the same fraction as the levels; a vector's direction is right
	// from its first sample, so it needs no priming
	if (r->count && r->readings[0].readout.peak) {
		uint32_t p_1 = (uint32_t)lrint(r->readings[0].readout.frequency / delta_f);
		for (uint32_t k = 1; k <= r->count; k++) {
			harmonic_reading_t *reading = &r->readings[k - 1];
			if (!reading->readout.peak) continue;
			uint32_t p_k = (uint32_t)lrint(reading->readout.frequency / delta_f);
			float theta = harmonic_phase(spectrum, n, p_k, p_1, k) * (float)(M_PI / 180.0);
			r->phases[k - 1].re += alpha * (cosf(theta) - r->phases[k - 1].re);
			r->phases[k - 1].im += alpha * (sinf(theta) - r->phases[k - 1].im);
			reading->phase_deg = atan2f(r->phases[k - 1].im, r->phases[k - 1].re) * (float)(180.0 / M_PI);
		}
	}

	// the distortion counts what is shown
	harmonic_readout_t reads[HARMONIC_MAX];
	int shown[HARMONIC_MAX];
	for (uint32_t k = 0; k < r->count; k++) {
		reads[k] = r->readings[k].readout;
		shown[k] = r->readings[k].shown;
	}
	r->has_thd = harmonic_thd(reads, shown, r->count, &r->thd) == 0;
}

int harmonic_reader_restarted(const harmonic_reader_t *r) {
	return r->restarted;
}

const harmonic_reading_t *harmonic_reader_readings(const harmonic_reader_t *r, uint32_t *count) {
	*count = r->count;
	return r->readings;
}

int harmonic_reader_thd(const harmonic_reader_t *r, float *thd) {
	if (!r->has_thd) return -1;
	*thd = r->thd;
	return 0;
}
