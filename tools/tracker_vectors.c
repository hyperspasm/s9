// The two-stage tracker over a mono float32 file at 48 kHz: audio fed in 10 ms
// hops, and after each a line of the instant the reported lock stands at, the
// lock's frequency and confidence, the fundamental, whether it is committed, and
// the harmonicity. For the tracker harness in validation/tracker, which scores
// the trajectory. With -v, each line also carries the lock's period in samples,
// its rate of change per hop, the smoothed magnitude of that rate, the tracking
// window in samples, the decision's numerator and denominator, the octave and
// double-octave sub-multiples' evidence, and the frame's peak count, to trace both
// stages.
//   tracker_vectors [-v] IN.f32
#include "tracker.h"
#include <stdio.h>
#include <stdlib.h>

#define FS 48000
#define HOP 480   // 10 ms

int main(int argc, char **argv) {
	int verbose = argc > 2 && argv[1][0] == '-' && argv[1][1] == 'v';
	const char *path = argv[argc - 1];
	if (argc < 2) { fprintf(stderr, "usage: tracker_vectors [-v] IN.f32\n"); return 2; }
	FILE *in = fopen(path, "rb");
	if (!in) { perror(path); return 1; }
	tracker_t *t = tracker_new(NULL, FS);
	if (!t) { fprintf(stderr, "no tracker\n"); return 1; }
	float hop[HOP];
	unsigned long fed = 0;
	while (fread(hop, sizeof(float), HOP, in) == HOP) {
		tracker_feed(t, hop, HOP);
		fed += HOP;
		tracker_state_t s = tracker_state(t);
		printf("%.6f %.4f %.4f %.4f %d %.4f", (double)fed / FS - s.delay_s, s.lock_hz, s.lock_confidence, s.fundamental_hz, s.committed ? 1 : 0, s.harmonicity);
		if (verbose) {
			double period, rate, slew;
			uint32_t window;
			tracker_lock_internals(t, &period, &rate, &slew, &window);
			int g, m;
			double sub_seen[2];
			uint32_t peaks;
			tracker_fundamental_internals(t, &g, &m, sub_seen, &peaks);
			printf(" %.4f %.6f %.6f %u %d %d %.3f %.3f %u", period, rate, slew, window, g, m, sub_seen[0], sub_seen[1], peaks);
		}
		printf("\n");
	}
	fclose(in);
	tracker_free(&t);
	return 0;
}
