// The fundamental tracker over a mono float32 file at 48 kHz, as it runs in s9:
// audio fed in 10 ms display frames, one update per frame, and after each a
// line of the instant the frame's transform is centred on, the frequency
// reported, whether it is valid, and the estimate's harmonicity. For the
// tracker harness in validation/tracker, which scores the trajectory.
//   track_vectors IN.f32
#include "fundamental.h"
#include <stdio.h>
#include <stdlib.h>

#define FS 48000
#define HOP 480   // 10 ms

int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: track_vectors IN.f32\n"); return 2; }
	FILE *in = fopen(argv[1], "rb");
	if (!in) { perror(argv[1]); return 1; }
	fundamental_t *t = fundamental_new(NULL);
	if (!t) { fprintf(stderr, "no tracker\n"); return 1; }
	fundamental_prepare_to_stream(t, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 1, .buffer_frames = HOP, .samples_per_display_frame = HOP });
	double centre = -(double)fundamental_fft_len(t) / 2.0 / FS;   // the transform is of the last fft_len samples
	float hop[HOP];
	unsigned long fed = 0;
	while (fread(hop, sizeof(float), HOP, in) == HOP) {
		fundamental_enqueue_audio_data(t, hop, sizeof hop);
		fundamental_update(t);
		fed += HOP;
		printf("%.6f %.4f %d %.4f\n", (double)fed / FS + centre, fundamental_frequency(t), fundamental_valid(t) ? 1 : 0, fundamental_harmonicity(t));
	}
	fclose(in);
	fundamental_free(&t);
	return 0;
}
