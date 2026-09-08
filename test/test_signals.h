#ifndef test_signals_h
#define test_signals_h

// Deterministic float32 test signals: the same samples the validation oracle
// generates (validation/src/s9/signals.py), computed in double in the same
// order of operations and rounded once.

#include <math.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { SIGNAL_SINE, SIGNAL_SAWTOOTH, SIGNAL_SQUARE, SIGNAL_TWIN, SIGNAL_NOISE, SIGNAL_SILENCE };

typedef struct {
	int kind;
	double freq, freq2, amp, phase;
	unsigned harmonics, seed;
} test_signal_t;

static inline void test_signal_generate(const test_signal_t *s, float *out, uint32_t n, double fs) {
	uint32_t state = s->seed;
	for (uint32_t i = 0; i < n; i++) {
		double t = (double)i / fs;
		double x = 0.0;
		switch (s->kind) {
		case SIGNAL_SINE:
			x = s->amp * sin(2.0 * M_PI * s->freq * t + s->phase);
			break;
		case SIGNAL_SAWTOOTH:
			for (unsigned k = 1; k <= s->harmonics; k++)
				x += (s->amp * 2.0 / (M_PI * k)) * ((k + 1) % 2 ? -1.0 : 1.0) * sin(2.0 * M_PI * k * s->freq * t + k * s->phase);
			break;
		case SIGNAL_SQUARE:
			for (unsigned k = 1; k <= s->harmonics; k += 2)
				x += (s->amp * 4.0 / (M_PI * k)) * sin(2.0 * M_PI * k * s->freq * t + k * s->phase);
			break;
		case SIGNAL_TWIN:
			x = s->amp * sin(2.0 * M_PI * s->freq * t + s->phase) + s->amp * sin(2.0 * M_PI * s->freq2 * t + s->phase);
			break;
		case SIGNAL_NOISE:
			state ^= state << 13;
			state ^= state >> 17;
			state ^= state << 5;
			x = s->amp * ((state >> 8) * (2.0 / 16777216.0) - 1.0);
			break;
		default:
			break;
		}
		out[i] = (float)x;
	}
}

static inline double test_signal_rms(const float *x, uint32_t n) {
	double sum = 0.0;
	for (uint32_t i = 0; i < n; i++) sum += (double)x[i] * x[i];
	return n ? sqrt(sum / n) : 0.0;
}

#endif /* test_signals_h */
