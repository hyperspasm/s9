// The analyzer's transforms and normalized autocorrelation, against the
// validation oracle's golden and against known signals.
#include "greatest.h"
#include "fft_analyzer.h"
#include "test_signals.h"
#include "pitch_golden.h"

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

static float bin_amplitude(const fft_analyzer_t *fa, uint32_t index, uint32_t bin) {
	const fftwf_complex *s = fft_analyzer_spectrum(fa, index);
	return sqrtf(s[bin][0] * s[bin][0] + s[bin][1] * s[bin][1]) * 2.0f / fft_analyzer_fft_len(fa);
}

// The normalized autocorrelation of the golden's frame matches the oracle at every lag.
TEST nacf_matches_golden(void) {
	const pitch_golden_signal_t *g = &pitch_golden_nacf_signal;
	test_signal_t s = { g->kind, g->freq, g->freq2, g->amp, g->phase, g->harmonics, g->seed };
	float *x = malloc(sizeof(float) * PITCH_GOLDEN_NACF_N);
	test_signal_generate(&s, x, PITCH_GOLDEN_NACF_N, FS);

	fft_analyzer_t *fa = make(PITCH_GOLDEN_NACF_N, FFT_ANALYZER_MIX, 0,
							  (window_function_t)pitch_golden_nacf_window.function, (float)pitch_golden_nacf_window.skew, 1);
	feed(fa, x, PITCH_GOLDEN_NACF_N, 1);

	float *nacf = malloc(sizeof(float) * PITCH_GOLDEN_NACF_LAGS);
	ASSERT_EQ(0, fft_analyzer_nacf(fa, 0, nacf, PITCH_GOLDEN_NACF_LAGS));
	ASSERT_IN_RANGE(1.0f, nacf[0], 1e-6f);
	for (uint32_t t = 0; t < PITCH_GOLDEN_NACF_LAGS; t++)
		ASSERT_IN_RANGEm("lag", pitch_golden_nacf[t], (double)nacf[t], 5e-4);

	free(nacf);
	free(x);
	fft_analyzer_free(&fa);
	PASS();
}

// A sine of amplitude a on a bin centre reads a at that bin.
TEST spectrum_is_calibrated(void) {
	enum { N = 4096, BIN = 100 };
	float *x = malloc(sizeof(float) * N);
	test_signal_t s = { SIGNAL_SINE, (double)BIN * FS / N, 0, 0.5, 0.3, 0, 0 };
	test_signal_generate(&s, x, N, FS);

	fft_analyzer_t *fa = make(N, FFT_ANALYZER_EACH_CHANNEL, 0, WINDOW_FUNCTION_HANN, 0.0f, 1);
	feed(fa, x, N, 1);
	ASSERT_EQ(1u, fft_analyzer_transforms(fa));
	ASSERT_IN_RANGE(0.5f, bin_amplitude(fa, 0, BIN), 1e-3f);
	ASSERT(bin_amplitude(fa, 0, BIN + 10) < 1e-3f);
	ASSERT_IN_RANGE(0.5f / sqrtf(2.0f), fft_analyzer_rms(fa, 0), 1e-3f);

	free(x);
	fft_analyzer_free(&fa);
	PASS();
}

// Mixing transforms the selected channel, or the channels averaged.
TEST mix_selects_a_channel_or_averages(void) {
	enum { N = 2048, BIN0 = 50, BIN1 = 80 };
	float *left = malloc(sizeof(float) * N), *right = malloc(sizeof(float) * N), *stereo = malloc(sizeof(float) * N * 2);
	test_signal_t s0 = { SIGNAL_SINE, (double)BIN0 * FS / N, 0, 0.8, 0, 0, 0 };
	test_signal_t s1 = { SIGNAL_SINE, (double)BIN1 * FS / N, 0, 0.4, 0, 0, 0 };
	test_signal_generate(&s0, left, N, FS);
	test_signal_generate(&s1, right, N, FS);
	for (int i = 0; i < N; i++) { stereo[2 * i] = left[i]; stereo[2 * i + 1] = right[i]; }

	fft_analyzer_t *fa = make(N, FFT_ANALYZER_MIX, 0, WINDOW_FUNCTION_HANN, 0.0f, 2);
	feed(fa, stereo, N, 2);
	ASSERT_EQ(1u, fft_analyzer_transforms(fa));
	ASSERT_IN_RANGE(0.8f, bin_amplitude(fa, 0, BIN0), 1e-3f);
	ASSERT(bin_amplitude(fa, 0, BIN1) < 1e-3f);

	fft_analyzer_set_channel(fa, 1);
	fft_analyzer_update(fa);
	ASSERT(bin_amplitude(fa, 0, BIN0) < 1e-3f);
	ASSERT_IN_RANGE(0.4f, bin_amplitude(fa, 0, BIN1), 1e-3f);

	fft_analyzer_set_channel(fa, FFT_ANALYZER_CHANNEL_ALL);
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
	test_signal_t s = { SIGNAL_SINE, 100.0 * FS / N, 0, 0.5, 0, 0, 0 };
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
	RUN_TEST(nacf_matches_golden);
	RUN_TEST(spectrum_is_calibrated);
	RUN_TEST(mix_selects_a_channel_or_averages);
	RUN_TEST(history_slides_by_whole_display_frames);
	RUN_TEST(fft_len_from_resolution);
	GREATEST_MAIN_END();
}
