// The spectrogram's readout timed at the display's defaults, for sizing a
// machine: for each resolution, the frame the bottom of the range needs and
// the latency that is, the kernels' build time and taps, the readout per
// row with the transforms' share of it, and what that is of one core at the
// default hop.
//
//   cq_bench [cents ...]     the resolutions to time; 200, 100 and 50 by default
#include "constant_q.h"
#include "fft.h"
#include "window_function.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define FS 48000.0
#define HOP 480          // frames: 10 ms
#define COLUMNS 1200
#define F_MIN 20.0
#define F_MAX 20000.0
#define ROWS 100         // readouts timed

static double now(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void run(int cents) {
	double semitone = pow(2.0, cents / 1200.0) - 1.0;
	uint32_t n = fft_fast_length((uint32_t)ceil(window_function_bandwidth(WINDOW_FUNCTION_BLACKMAN_HARRIS) * FS / (F_MIN * semitone)));
	double edges[COLUMNS + 1];
	for (int c = 0; c <= COLUMNS; c++) edges[c] = F_MIN * pow(F_MAX / F_MIN, (double)c / COLUMNS);
	constant_q_config_t cfg = {
		.sample_rate = FS, .n = n, .min_window = 2 * HOP, .resolution_semitones = cents / 100.0f,
		.window_function = WINDOW_FUNCTION_BLACKMAN_HARRIS, .threshold_db = -100.0f, .readouts_per_ridge = 3.0f,
	};
	double t0 = now();
	constant_q_t *cq = constant_q_new(&cfg, edges, COLUMNS);
	double built = now() - t0;
	if (!cq) { fprintf(stderr, "%d cents: no readout\n", cents); return; }

	float *frame = malloc(sizeof(float) * n), *bins = malloc(sizeof(float) * (n + 2)), db[COLUMNS];
	for (uint32_t i = 0; i < n; i++) frame[i] = (float)rand() / (float)RAND_MAX - 0.5f;

	// the transforms alone, one per level
	double transforms = 0.0;
	for (uint32_t l = 0; l < constant_q_levels(cq); l++) {
		fft_t *fft = fft_new(constant_q_level_length(cq, l), FFT_REAL);
		t0 = now();
		for (int r = 0; r < ROWS; r++) fft_forward_real(fft, frame, bins);
		transforms += (now() - t0) / ROWS;
		fft_free(&fft);
	}

	t0 = now();
	for (int r = 0; r < ROWS; r++) constant_q_readout(cq, frame, db);
	double per = (now() - t0) / ROWS;

	printf("%3d cents: frame %6u (%.2f s, latency %.2f s), kernels built in %.1f s, %u taps in %u levels;\n"
	       "           readout %.2f ms per row, %.2f ms of it the transforms; %.0f%% of a core at %d ms hops\n",
	       cents, n, n / FS, n / FS / 2.0, built, constant_q_taps(cq), constant_q_levels(cq),
	       per * 1e3, transforms * 1e3, per / (HOP / FS) * 100.0, (int)(HOP / FS * 1000.0));
	constant_q_free(&cq);
	free(frame);
	free(bins);
}

int main(int argc, char **argv) {
	if (argc > 1) {
		for (int i = 1; i < argc; i++) run(atoi(argv[i]));
	} else {
		run(200);
		run(100);
		run(50);
	}
	return 0;
}
