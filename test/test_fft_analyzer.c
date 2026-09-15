// The analyzer's transforms, against known signals.
#include "greatest.h"
#include "fft_analyzer.h"
#include "test_signals.h"

#include <stdlib.h>
#include <string.h>

#define FS 44100

static fft_analyzer_t *make(uint32_t fft_len, fft_analyzer_mode_t mode, uint32_t channel,
							window_function_t window, float skew, uint32_t channels) {
	fft_analyzer_t *fa = fft_analyzer_new(&(fft_analyzer_config_t){
		.fft_len = fft_len, .window_function = window, .window_skew = skew, .mode = mode, .channel = channel,
	});
	fft_analyzer_prepare_to_stream(fa, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = channels,
		.buffer_frames = 256, .samples_per_display_frame = fft_len,
	});
	return fa;
}

static void feed(fft_analyzer_t *fa, const float *interleaved, uint32_t frames, uint32_t channels) {
	fft_analyzer_enqueue_audio_data(fa, interleaved, frames * channels * sizeof(float));
	fft_analyzer_update(fa);
}

static float bin_power(const fft_analyzer_t *fa, uint32_t index, uint32_t bin) {
	const float *s = fft_analyzer_spectrum(fa, index);
	return s[2 * bin] * s[2 * bin] + s[2 * bin + 1] * s[2 * bin + 1];
}

static float bin_amplitude(const fft_analyzer_t *fa, uint32_t index, uint32_t bin) {
	return sqrtf(bin_power(fa, index, bin)) * 2.0f / fft_analyzer_fft_len(fa);
}

// the configuration with one channel selected
static void select_channel(fft_analyzer_t *fa, uint32_t channel) {
	fft_analyzer_config_t config = *fft_analyzer_config(fa);
	config.channel = channel;
	fft_analyzer_set_config(fa, &config);
}

// A sine of amplitude a on a bin centre reads a at that bin.
TEST spectrum_is_calibrated(void) {
	enum { N = 4096, BIN = 100 };
	float *x = malloc(sizeof(float) * N);
	test_signal_t s = { SIGNAL_SINE, (double)BIN * FS / N, 0, 0.5, 0.3, 0, 0, 0 };
	test_signal_generate(&s, x, N, FS);

	fft_analyzer_t *fa = make(N, FFT_ANALYZER_EACH_CHANNEL, 0, WINDOW_FUNCTION_HANN, 0.0f, 1);
	feed(fa, x, N, 1);
	ASSERT_EQ(1u, fft_analyzer_transforms(fa));
	ASSERT_IN_RANGE(0.5f, bin_amplitude(fa, 0, BIN), 1e-3f);
	ASSERT(bin_amplitude(fa, 0, BIN + 10) < 1e-3f);
	ASSERT_IN_RANGE(0.5f / sqrtf(2.0f), fft_analyzer_rms(fa, 0), 1e-3f);
	ASSERT_EQ(0, memcmp(x, fft_analyzer_frame(fa, 0), sizeof(float) * N));   // the frame as fed, unwindowed

	free(x);
	fft_analyzer_free(&fa);
	PASS();
}

// Mixing transforms the selected channel, or the channels averaged.
TEST mix_selects_a_channel_or_averages(void) {
	enum { N = 2048, BIN0 = 50, BIN1 = 80 };
	float *left = malloc(sizeof(float) * N), *right = malloc(sizeof(float) * N), *stereo = malloc(sizeof(float) * N * 2);
	test_signal_t s0 = { SIGNAL_SINE, (double)BIN0 * FS / N, 0, 0.8, 0, 0, 0, 0 };
	test_signal_t s1 = { SIGNAL_SINE, (double)BIN1 * FS / N, 0, 0.4, 0, 0, 0, 0 };
	test_signal_generate(&s0, left, N, FS);
	test_signal_generate(&s1, right, N, FS);
	for (int i = 0; i < N; i++) { stereo[2 * i] = left[i]; stereo[2 * i + 1] = right[i]; }

	fft_analyzer_t *fa = make(N, FFT_ANALYZER_MIX, 0, WINDOW_FUNCTION_HANN, 0.0f, 2);
	feed(fa, stereo, N, 2);
	ASSERT_EQ(1u, fft_analyzer_transforms(fa));
	ASSERT_IN_RANGE(0.8f, bin_amplitude(fa, 0, BIN0), 1e-3f);
	ASSERT(bin_amplitude(fa, 0, BIN1) < 1e-3f);

	select_channel(fa, 1);
	fft_analyzer_update(fa);
	ASSERT(bin_amplitude(fa, 0, BIN0) < 1e-3f);
	ASSERT_IN_RANGE(0.4f, bin_amplitude(fa, 0, BIN1), 1e-3f);

	select_channel(fa, S9_CHANNEL_ALL);
	fft_analyzer_update(fa);
	ASSERT_IN_RANGE(0.4f, bin_amplitude(fa, 0, BIN0), 1e-3f);
	ASSERT_IN_RANGE(0.2f, bin_amplitude(fa, 0, BIN1), 1e-3f);

	fft_analyzer_t *each = make(N, FFT_ANALYZER_EACH_CHANNEL, 0, WINDOW_FUNCTION_HANN, 0.0f, 2);
	feed(each, stereo, N, 2);
	ASSERT_EQ(2u, fft_analyzer_transforms(each));
	ASSERT_IN_RANGE(0.8f, bin_amplitude(each, 0, BIN0), 1e-3f);
	ASSERT_IN_RANGE(0.4f, bin_amplitude(each, 1, BIN1), 1e-3f);

	free(left); free(right); free(stereo);
	fft_analyzer_free(&fa);
	fft_analyzer_free(&each);
	PASS();
}

// Without new audio, an update keeps the previous frame, and a partial
// display frame waits until it completes.
TEST history_slides_by_whole_display_frames(void) {
	enum { N = 1024 };
	float *x = malloc(sizeof(float) * N * 2);
	test_signal_t s = { SIGNAL_SINE, 100.0 * FS / N, 0, 0.5, 0, 0, 0, 0 };
	test_signal_generate(&s, x, N * 2, FS);

	fft_analyzer_t *fa = fft_analyzer_new(&(fft_analyzer_config_t){ .fft_len = N, .window_function = WINDOW_FUNCTION_HANN, .mode = FFT_ANALYZER_MIX });
	fft_analyzer_prepare_to_stream(fa, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 1, .buffer_frames = 256, .samples_per_display_frame = N / 4,
	});
	feed(fa, x, N, 1);
	float before = bin_amplitude(fa, 0, 100);
	ASSERT_IN_RANGE(0.5f, before, 1e-3f);

	fft_analyzer_update(fa);
	ASSERT_EQ_FMT(before, bin_amplitude(fa, 0, 100), "%g");

	// fewer than a display frame's samples are not consumed yet
	feed(fa, x, N / 8, 1);
	ASSERT_EQ_FMT(before, bin_amplitude(fa, 0, 100), "%g");

	free(x);
	fft_analyzer_free(&fa);
	PASS();
}

// A new length takes effect at the next update, restarting the history;
// a new averaging time does not touch the history.
TEST reconfigure_at_next_update(void) {
	enum { N = 1024 };
	float *x = malloc(sizeof(float) * N * 2);
	test_signal_t s = { SIGNAL_SINE, 100.0 * FS / N, 0, 0.5, 0, 0, 0, 0 };
	test_signal_generate(&s, x, N * 2, FS);

	fft_analyzer_t *fa = make(N, FFT_ANALYZER_MIX, 0, WINDOW_FUNCTION_HANN, 0.0f, 1);
	feed(fa, x, N, 1);
	ASSERT_IN_RANGE(0.5f, bin_amplitude(fa, 0, 100), 1e-3f);

	fft_analyzer_config_t config = *fft_analyzer_config(fa);
	config.fft_len = N * 2;
	fft_analyzer_set_config(fa, &config);
	ASSERT_EQ(N, fft_analyzer_fft_len(fa));
	feed(fa, x, N * 2, 1);
	ASSERT_EQ(N * 2, fft_analyzer_fft_len(fa));
	ASSERT_IN_RANGE(0.5f, bin_amplitude(fa, 0, 200), 1e-3f);

	config.averaging_s = 1.0f;
	fft_analyzer_set_config(fa, &config);
	fft_analyzer_update(fa);   // nothing new consumed: an untouched history reads the same
	ASSERT_IN_RANGE(0.5f, bin_amplitude(fa, 0, 200), 1e-3f);

	free(x);
	fft_analyzer_free(&fa);
	PASS();
}

// The power average follows audio time: after one time constant of
// silence it has fallen to 1/e of a steady tone's power, whatever the
// update rate, and a reset restarts it from the next transform.
TEST power_average_follows_audio_time(void) {
	enum { N = 1024, BIN = 100, HOPS = 4 };
	uint32_t hops[2] = { N, 2 * N };   // two update rates, each update a whole new frame
	float tau = (float)(HOPS * N) / FS;
	float *x = malloc(sizeof(float) * N * 2), *silence = calloc(N * 2, sizeof(float));
	test_signal_t s = { SIGNAL_SINE, (double)BIN * FS / N, 0, 0.5, 0, 0, 0, 0 };
	test_signal_generate(&s, x, N * 2, FS);

	for (int h = 0; h < 2; h++) {
		fft_analyzer_t *fa = fft_analyzer_new(&(fft_analyzer_config_t){
			.fft_len = N, .window_function = WINDOW_FUNCTION_HANN, .mode = FFT_ANALYZER_MIX, .averaging_s = tau,
		});
		fft_analyzer_prepare_to_stream(fa, &(s9_audio_stream_config_t){
			.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 1, .buffer_frames = 256, .samples_per_display_frame = hops[h],
		});
		for (int i = 0; i < 40; i++) feed(fa, x, hops[h], 1);   // long enough to settle
		float steady = fft_analyzer_power(fa, 0)[BIN];
		ASSERT_IN_RANGE(bin_power(fa, 0, BIN), steady, steady * 1e-4f);

		uint32_t updates = HOPS * N / hops[h];   // one time constant of silence
		for (uint32_t i = 0; i < updates; i++) feed(fa, silence, hops[h], 1);
		ASSERT_IN_RANGE(steady * expf(-1.0f), fft_analyzer_power(fa, 0)[BIN], steady * 1e-3f);

		fft_analyzer_reset_power(fa);
		feed(fa, silence, hops[h], 1);
		ASSERT_EQ_FMT(0.0f, fft_analyzer_power(fa, 0)[BIN], "%g");
		fft_analyzer_free(&fa);
	}
	free(x);
	free(silence);
	PASS();
}

// The average does not start until the history has filled: a tone arriving
// in quarter frames reads its full power the moment the frame is full,
// with nothing of the partial frames in it, and averages from the next.
TEST power_average_waits_for_a_full_frame(void) {
	enum { N = 1024, BIN = 100, HOP = N / 4 };
	float *x = malloc(sizeof(float) * N);
	test_signal_t s = { SIGNAL_SINE, (double)BIN * FS / N, 0, 0.5, 0, 0, 0, 0 };
	test_signal_generate(&s, x, N, FS);

	fft_analyzer_t *fa = fft_analyzer_new(&(fft_analyzer_config_t){
		.fft_len = N, .window_function = WINDOW_FUNCTION_HANN, .mode = FFT_ANALYZER_MIX, .averaging_s = 10.0f,
	});
	fft_analyzer_prepare_to_stream(fa, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 1, .buffer_frames = 256, .samples_per_display_frame = HOP,
	});
	for (int i = 0; i < 4; i++) {
		feed(fa, x + i * HOP, HOP, 1);
		ASSERT_EQ_FMT(1.0f, fft_analyzer_average_fraction(fa), "%g");
	}
	ASSERT_EQ_FMT(bin_power(fa, 0, BIN), fft_analyzer_power(fa, 0)[BIN], "%g");
	feed(fa, x, HOP, 1);
	ASSERT(fft_analyzer_average_fraction(fa) < 1.0f);

	free(x);
	fft_analyzer_free(&fa);
	PASS();
}

// With hop_frames set, an update consumes exactly one hop and says so, or
// nothing while fewer frames are queued, and the history slides by the hop;
// it is filled once a whole frame has been consumed.
TEST hop_frames_step_the_history(void) {
	enum { N = 1024, HOP = 100 };
	float *ones = malloc(sizeof(float) * N), *silence = calloc(N, sizeof(float));
	for (int i = 0; i < N; i++) ones[i] = 1.0f;

	fft_analyzer_t *fa = fft_analyzer_new(&(fft_analyzer_config_t){
		.fft_len = N, .window_function = WINDOW_FUNCTION_HANN, .mode = FFT_ANALYZER_MIX, .hop_frames = HOP,
	});
	fft_analyzer_prepare_to_stream(fa, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 1, .buffer_frames = 256, .samples_per_display_frame = N / 4,
	});
	fft_analyzer_enqueue_audio_data(fa, silence, sizeof(float) * N);
	for (int i = 0; i < N / HOP; i++) ASSERT_EQ(HOP, fft_analyzer_update(fa));
	ASSERT_EQ(N % HOP, fft_analyzer_queued(fa));
	ASSERT_EQ(0u, fft_analyzer_update(fa));   // a partial hop waits
	ASSERT_EQ_FMT(0.0f, fft_analyzer_hop_seconds(fa), "%g");
	ASSERT(!fft_analyzer_filled(fa));

	fft_analyzer_enqueue_audio_data(fa, ones, sizeof(float) * 2 * HOP);
	ASSERT_EQ(HOP, fft_analyzer_update(fa));   // the partial hop of silence and the first of the ones
	ASSERT(fft_analyzer_filled(fa));
	ASSERT_IN_RANGE(sqrtf((float)(HOP - N % HOP) / N), fft_analyzer_rms(fa, 0), 1e-6f);
	ASSERT_EQ(HOP, fft_analyzer_update(fa));
	ASSERT_IN_RANGE(sqrtf((float)(2 * HOP - N % HOP) / N), fft_analyzer_rms(fa, 0), 1e-6f);
	ASSERT_IN_RANGE((float)HOP / FS, fft_analyzer_hop_seconds(fa), 1e-9f);
	ASSERT_EQ(N % HOP, fft_analyzer_queued(fa));

	free(ones);
	free(silence);
	fft_analyzer_free(&fa);
	PASS();
}

// With frame_only set, an update keeps the frame and its RMS but no
// spectrum or power; cleared, the next update transforms the same frame.
TEST frame_only_keeps_the_frame_and_skips_the_transform(void) {
	enum { N = 4096, BIN = 100 };
	float *x = malloc(sizeof(float) * N);
	test_signal_t s = { SIGNAL_SINE, (double)BIN * FS / N, 0, 0.5, 0.3, 0, 0, 0 };
	test_signal_generate(&s, x, N, FS);

	fft_analyzer_t *fa = fft_analyzer_new(&(fft_analyzer_config_t){
		.fft_len = N, .window_function = WINDOW_FUNCTION_HANN, .mode = FFT_ANALYZER_MIX, .frame_only = 1,
	});
	fft_analyzer_prepare_to_stream(fa, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 1, .buffer_frames = 256, .samples_per_display_frame = N,
	});
	feed(fa, x, N, 1);
	ASSERT(fft_analyzer_spectrum(fa, 0) == NULL);
	ASSERT(fft_analyzer_power(fa, 0) == NULL);
	ASSERT_EQ(0, memcmp(x, fft_analyzer_frame(fa, 0), sizeof(float) * N));
	ASSERT_IN_RANGE(0.5f / sqrtf(2.0f), fft_analyzer_rms(fa, 0), 1e-3f);

	fft_analyzer_config_t config = *fft_analyzer_config(fa);
	config.frame_only = 0;
	fft_analyzer_set_config(fa, &config);
	fft_analyzer_update(fa);
	ASSERT(fft_analyzer_spectrum(fa, 0) != NULL);
	ASSERT_IN_RANGE(0.5f, bin_amplitude(fa, 0, BIN), 1e-3f);

	free(x);
	fft_analyzer_free(&fa);
	PASS();
}

// The transform length is the power of two at or above sample_rate / resolution.
TEST fft_len_from_resolution(void) {
	fft_analyzer_t *fa = fft_analyzer_new(&(fft_analyzer_config_t){ .resolution_hz = 2.0f, .window_function = WINDOW_FUNCTION_HANN });
	fft_analyzer_prepare_to_stream(fa, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = 44100, .channels = 1, .buffer_frames = 256, .samples_per_display_frame = 735,
	});
	ASSERT_EQ(32768u, fft_analyzer_fft_len(fa));
	fft_analyzer_prepare_to_stream(fa, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = 48000, .channels = 1, .buffer_frames = 256, .samples_per_display_frame = 800,
	});
	ASSERT_EQ(32768u, fft_analyzer_fft_len(fa));
	fft_analyzer_free(&fa);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(spectrum_is_calibrated);
	RUN_TEST(mix_selects_a_channel_or_averages);
	RUN_TEST(history_slides_by_whole_display_frames);
	RUN_TEST(reconfigure_at_next_update);
	RUN_TEST(power_average_follows_audio_time);
	RUN_TEST(power_average_waits_for_a_full_frame);
	RUN_TEST(hop_frames_step_the_history);
	RUN_TEST(frame_only_keeps_the_frame_and_skips_the_transform);
	RUN_TEST(fft_len_from_resolution);
	GREATEST_MAIN_END();
}
