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

enum { SIGNAL_SINE, SIGNAL_SAWTOOTH, SIGNAL_SQUARE, SIGNAL_TWIN, SIGNAL_NOISE, SIGNAL_SILENCE, SIGNAL_IMPULSE };

typedef struct {
	int kind;
	double freq, freq2, amp, phase;
	unsigned harmonics, seed;
	double noise_amp;   // noise from the same seed added to the signal, when positive
} test_signal_t;

static inline float test_signal_noise(uint32_t *state, double amp) {
	*state ^= *state << 13;
	*state ^= *state >> 17;
	*state ^= *state << 5;
	return (float)(amp * ((*state >> 8) * (2.0 / 16777216.0) - 1.0));
}

static inline void test_signal_generate(const test_signal_t *s, float *out, uint32_t n, double fs) {
	uint32_t state = s->seed, added = s->seed;
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
			x = test_signal_noise(&state, s->amp);
			break;
		default:
			break;
		}
		out[i] = (float)x;
		if (s->noise_amp > 0.0) out[i] += test_signal_noise(&added, s->noise_amp);
	}
}

// A table of partials, count (frequency, amplitude, phase) triples, summed in
// order each times amp, plus noise of noise_amp from the seed when positive:
// signals.partials() and generate() in the oracle.
static inline void test_signal_partials(const double *table, unsigned count, double amp, unsigned seed, double noise_amp,
                                        float *out, uint32_t n, double fs) {
	uint32_t added = seed;
	for (uint32_t i = 0; i < n; i++) {
		double t = (double)i / fs;
		double x = 0.0;
		for (unsigned k = 0; k < count; k++)
			x += (amp * table[3 * k + 1]) * sin(2.0 * M_PI * table[3 * k] * t + table[3 * k + 2]);
		out[i] = (float)x;
		if (noise_amp > 0.0) out[i] += test_signal_noise(&added, noise_amp);
	}
}

// One sample of amp at index in silence: signals.impulse() in the oracle.
// The kind SIGNAL_IMPULSE names it in a table; test_signal_generate() has no
// index and leaves that kind silent.
static inline void test_signal_impulse(float *out, uint32_t n, uint32_t index, double amp) {
	for (uint32_t i = 0; i < n; i++) out[i] = 0.0f;
	if (index < n) out[index] = (float)amp;
}

static inline double test_signal_rms(const float *x, uint32_t n) {
	double sum = 0.0;
	for (uint32_t i = 0; i < n; i++) sum += (double)x[i] * x[i];
	return n ? sqrt(sum / n) : 0.0;
}

#endif /* test_signals_h */
