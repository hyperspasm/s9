#include "frequency_counter.h"
#include "s9.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CROSSINGS 8192            // the most crossings kept: past that the window shortens to what they span
#define ENVELOPE_S 0.1            // the peak's decay time constant, seconds

typedef enum {
	DISARMED,   // after a confirmed crossing: waits for the signal to go below the band
	ARMED,      // below the band: the next rise through zero is a candidate
	CROSSED,    // through zero: a candidate, confirmed once the signal clears the band
} phase_t;

struct frequency_counter {
	frequency_counter_config_t cfg;
	double fs;
	uint64_t total;          // samples fed
	double peak;             // the signal's envelope: its peak, decaying
	double decay;            // of the peak, per sample
	phase_t phase;
	float prev;              // the previous sample
	double candidate;        // the crossing awaiting confirmation, in samples from the stream's start
	double *crossings;       // a ring of confirmed crossings, in samples from the stream's start
	uint32_t count, head;    // held, and the slot the next goes to
};

frequency_counter_t *frequency_counter_new(const frequency_counter_config_t *config, uint32_t sample_rate) {
	CHECK_PARAMETER(sample_rate > 0, return NULL);
	frequency_counter_t *fc = calloc(1, sizeof(frequency_counter_t));
	CHECK_ALLOC(fc, return NULL);
	fc->cfg = config ? *config : default_frequency_counter_config;
	if (!(fc->cfg.window_s > 0.0f)) fc->cfg.window_s = default_frequency_counter_config.window_s;
	if (!(fc->cfg.hysteresis > 0.0f && fc->cfg.hysteresis < 1.0f)) fc->cfg.hysteresis = default_frequency_counter_config.hysteresis;
	if (!(fc->cfg.regularity > 0.0f)) fc->cfg.regularity = default_frequency_counter_config.regularity;
	fc->fs = sample_rate;
	fc->decay = exp(-1.0 / (ENVELOPE_S * fc->fs));
	fc->crossings = calloc(CROSSINGS, sizeof(double));
	CHECK_ALLOC(fc->crossings, free(fc); return NULL);
	frequency_counter_reset(fc);
	return fc;
}

void frequency_counter_free(frequency_counter_t **fc) {
	if (!fc || !*fc) return;
	free((*fc)->crossings);
	free(*fc);
	*fc = NULL;
}

void frequency_counter_reset(frequency_counter_t *fc) {
	fc->total = 0;
	fc->peak = 0.0;
	fc->phase = DISARMED;
	fc->prev = 0.0f;
	fc->candidate = 0.0;
	fc->count = fc->head = 0;
}

void frequency_counter_feed(frequency_counter_t *fc, const float *samples, uint32_t count) {
	for (uint32_t i = 0; i < count; i++) {
		float x = samples[i];
		double at = (double)fc->total;   // this sample's time
		double magnitude = fabs((double)x);
		fc->peak = magnitude > fc->peak * fc->decay ? magnitude : fc->peak * fc->decay;
		double band = fc->cfg.hysteresis * fc->peak;
		switch (fc->phase) {
		case DISARMED:
			if (x < -band) fc->phase = ARMED;
			break;
		case ARMED:
			if (fc->prev < 0.0f && x >= 0.0f) {
				// through zero between the previous sample and this one: where a line between them crosses
				fc->candidate = at - 1.0 + (double)(-fc->prev) / (double)(x - fc->prev);
				fc->phase = CROSSED;
			}
			break;
		case CROSSED:
			if (x >= band) {
				fc->crossings[fc->head] = fc->candidate;
				fc->head = (fc->head + 1) % CROSSINGS;
				if (fc->count < CROSSINGS) fc->count++;
				fc->phase = DISARMED;
			} else if (x < 0.0f) {
				fc->phase = ARMED;   // a wiggle, not a crossing: the next rise through zero is the candidate
			}
			break;
		}
		fc->prev = x;
		fc->total++;
	}
}

bool frequency_counter_read(const frequency_counter_t *fc, float *frequency) {
	*frequency = 0.0f;
	if (fc->count < 3) return false;
	if (fc->peak < pow(10.0, fc->cfg.gate_dbfs / 20.0)) return false;
	double now = (double)fc->total;
	double start = now - (double)fc->cfg.window_s * fc->fs;
	// the crossings within the window, newest first: their span, their number, and the least
	// regular period among them
	uint32_t n = 0;
	double last = 0.0, first = 0.0, previous = 0.0;
	double shortest = INFINITY, longest = 0.0;
	for (uint32_t i = 0; i < fc->count; i++) {
		double t = fc->crossings[(fc->head + CROSSINGS - 1 - i) % CROSSINGS];
		if (t < start) break;
		if (n == 0) last = t;
		else {
			double period = previous - t;
			if (period < shortest) shortest = period;
			if (period > longest) longest = period;
		}
		first = t;
		previous = t;
		n++;
	}
	if (n < 3) return false;
	double mean = (last - first) / (n - 1);
	if (!(mean > 0.0)) return false;
	if (longest - mean > fc->cfg.regularity * mean || mean - shortest > fc->cfg.regularity * mean) return false;
	if (now - last > 3.0 * mean) return false;   // the signal has stopped crossing
	*frequency = (float)(fc->fs / mean);
	return true;
}
