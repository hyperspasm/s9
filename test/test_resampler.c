#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include "resampler.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 48000
#define BUF_SIZE    4096

// ---- signal generators ----

static void generate_sine(float *buf, uint32_t n, float freq, float amplitude, uint32_t sample_rate) {
	for (uint32_t i = 0; i < n; i++) {
		buf[i] = amplitude * sinf(2.0f * (float)M_PI * freq * (float)i / (float)sample_rate);
	}
}

static void generate_ramp(float *buf, uint32_t n, float start, float end) {
	for (uint32_t i = 0; i < n; i++) {
		buf[i] = start + (end - start) * (float)i / (float)(n - 1);
	}
}

static void generate_dc(float *buf, uint32_t n, float value) {
	for (uint32_t i = 0; i < n; i++) {
		buf[i] = value;
	}
}

// generate interleaved multi-channel data, each channel gets its own generator
__attribute__((unused))
static void generate_interleaved(float *buf, uint32_t frames, uint32_t channels,
                                  void (*gen)(float*, uint32_t, void*), void **gen_args) {
	float *tmp = malloc(frames * sizeof(float));
	assert(tmp != NULL);

	for (uint32_t c = 0; c < channels; c++) {
		gen(tmp, frames, gen_args[c]);
		for (uint32_t i = 0; i < frames; i++) {
			buf[i * channels + c] = tmp[i];
		}
	}
	free(tmp);
}

typedef struct { float start, end; } ramp_args_t;
__attribute__((unused))
static void ramp_gen(float *buf, uint32_t n, void *args) {
	ramp_args_t *a = (ramp_args_t*)args;
	generate_ramp(buf, n, a->start, a->end);
}

typedef struct { float value; } dc_args_t;
__attribute__((unused))
static void dc_gen(float *buf, uint32_t n, void *args) {
	dc_args_t *a = (dc_args_t*)args;
	generate_dc(buf, n, a->value);
}

typedef struct { float freq, amp; uint32_t sr; } sine_args_t;
__attribute__((unused))
static void sine_gen(float *buf, uint32_t n, void *args) {
	sine_args_t *a = (sine_args_t*)args;
	generate_sine(buf, n, a->freq, a->amp, a->sr);
}

// ---- lifecycle tests ----

static void test_new_free(void) {
	resampler_t *r = resampler_new(2, BUF_SIZE);
	assert(r != NULL);
	resampler_free(&r);
	assert(r == NULL);
}

static void test_new_various_channels(void) {
	// test 1, 2, 4, 8 channels
	uint32_t channel_counts[] = {1, 2, 4, 8};
	for (uint32_t i = 0; i < 4; i++) {
		resampler_t *r = resampler_new(channel_counts[i], BUF_SIZE);
		assert(r != NULL);
		resampler_free(&r);
	}
}

static void test_new_minimum_buffer(void) {
	// N=17, minimum is N+1=18. Power-of-two rounds up to 32.
	resampler_t *r = resampler_new(1, 18);
	assert(r != NULL);
	resampler_free(&r);
}

static void test_new_buffer_too_small(void) {
	// buffer_size < N+1=18 should fail
	resampler_t *r = resampler_new(1, 17);
	assert(r == NULL);
}

static void test_new_zero_channels(void) {
	resampler_t *r = resampler_new(0, BUF_SIZE);
	assert(r == NULL);
}

static void test_free_null_safety(void) {
	resampler_free(NULL);
	resampler_t *r = NULL;
	resampler_free(&r);
	// should not crash
}

// ---- buffer size rounding ----

static void test_buffer_rounds_to_power_of_2(void) {
	// enqueue should handle buffer that's been rounded up
	resampler_t *r = resampler_new(1, 100);  // rounds to 128
	assert(r != NULL);

	float samples[200];
	generate_dc(samples, 200, 0.5f);

	// should be able to enqueue ~127 frames (128 - 1 for empty slot)
	uint32_t enqueued = resampler_enqueue(r, samples, 127, 1);
	assert(enqueued == 127);

	resampler_free(&r);
}

// ---- enqueue tests ----

static void test_enqueue_mono(void) {
	resampler_t *r = resampler_new(1, BUF_SIZE);
	float samples[256];
	generate_dc(samples, 256, 0.5f);

	uint32_t enqueued = resampler_enqueue(r, samples, 256, 1);
	assert(enqueued == 256);

	resampler_free(&r);
}

static void test_enqueue_stereo(void) {
	resampler_t *r = resampler_new(2, BUF_SIZE);

	// interleaved stereo: [L0, R0, L1, R1, ...]
	float samples[512];
	for (uint32_t i = 0; i < 256; i++) {
		samples[i * 2]     = (float)i / 255.0f;  // left channel (ramp)
		samples[i * 2 + 1] = -1.0f;              // right channel (DC)
	}

	uint32_t enqueued = resampler_enqueue(r, samples, 256, 2);
	assert(enqueued == 256);

	resampler_free(&r);
}

static void test_enqueue_multi_channel(void) {
	resampler_t *r = resampler_new(4, BUF_SIZE);

	float samples[1024];  // 256 frames * 4 channels
	for (uint32_t i = 0; i < 256; i++) {
		samples[i * 4 + 0] = 0.1f;
		samples[i * 4 + 1] = 0.2f;
		samples[i * 4 + 2] = 0.3f;
		samples[i * 4 + 3] = 0.4f;
	}

	uint32_t enqueued = resampler_enqueue(r, samples, 256, 4);
	assert(enqueued == 256);

	resampler_free(&r);
}

static void test_enqueue_overflow(void) {
	// buffer_size=32 (already power-of-two), usable capacity = 31
	resampler_t *r = resampler_new(1, 32);
	float samples[64];
	generate_dc(samples, 64, 0.5f);

	uint32_t enqueued = resampler_enqueue(r, samples, 64, 1);
	assert(enqueued <= 31);

	resampler_free(&r);
}

static void test_enqueue_channel_mismatch(void) {
	resampler_t *r = resampler_new(2, BUF_SIZE);

	float samples[256];
	generate_dc(samples, 256, 0.5f);

	// enqueue with wrong channel count (1 instead of 2)
	uint32_t enqueued = resampler_enqueue(r, samples, 128, 1);
	assert(enqueued == 0);  // should reject

	// enqueue with correct channel count
	enqueued = resampler_enqueue(r, samples, 128, 2);
	assert(enqueued == 128);

	resampler_free(&r);
}

static void test_enqueue_null_guards(void) {
	resampler_t *r = resampler_new(1, BUF_SIZE);
	float samples[16] = {0};

	assert(resampler_enqueue(NULL, samples, 16, 1) == 0);
	assert(resampler_enqueue(r, NULL, 16, 1) == 0);
	assert(resampler_enqueue(r, samples, 0, 1) == 0);

	resampler_free(&r);
}

// ---- read guards ----

static void test_read_null_guards(void) {
	resampler_t *r = resampler_new(1, BUF_SIZE);
	float out[16] = {0};

	assert(resampler_read(NULL, out, 16, 1.0f, 1.0f) == 0);
	assert(resampler_read(r, NULL, 16, 1.0f, 1.0f) == 0);
	assert(resampler_read(r, out, 0, 1.0f, 1.0f) == 0);
	assert(resampler_read(r, out, 16, 0.0f, 1.0f) == 0);
	assert(resampler_read(r, out, 16, -1.0f, 1.0f) == 0);

	resampler_free(&r);
}

static void test_read_empty_buffer(void) {
	resampler_t *r = resampler_new(1, BUF_SIZE);

	float out[256];
	uint32_t read = resampler_read(r, out, 256, 1.0f, 1.0f);
	assert(read == 0);

	resampler_free(&r);
}

// ---- resample tests - mono ----

static void test_read_unity_ratio_mono(void) {
	resampler_t *r = resampler_new(1, BUF_SIZE);

	float samples[1024];
	generate_ramp(samples, 1024, -0.5f, 0.5f);
	resampler_enqueue(r, samples, 1024, 1);

	float out[512];
	uint32_t read = resampler_read(r, out, 512, 1.0f, 1.0f);

	assert(read > 0);

	// at 1:1 ratio, output of a ramp should be monotonically increasing
	for (uint32_t i = 1; i < read; i++) {
		assert(out[i] >= out[i - 1] - 0.001f);
	}

	resampler_free(&r);
}

static void test_read_upsample_mono(void) {
	resampler_t *r = resampler_new(1, BUF_SIZE);

	float samples[1024];
	generate_ramp(samples, 1024, -0.5f, 0.5f);
	resampler_enqueue(r, samples, 1024, 1);

	// 2x upsample
	float out[512];
	uint32_t read = resampler_read(r, out, 512, 2.0f, 1.0f);

	assert(read > 0);

	// upsampled ramp should still be monotonically increasing
	for (uint32_t i = 1; i < read; i++) {
		assert(out[i] >= out[i - 1] - 0.001f);
	}

	resampler_free(&r);
}

static void test_read_downsample_mono(void) {
	resampler_t *r = resampler_new(1, BUF_SIZE);

	float samples[2048];
	generate_ramp(samples, 2048, -0.5f, 0.5f);
	resampler_enqueue(r, samples, 2048, 1);

	// 4x downsample
	float out[256];
	uint32_t read = resampler_read(r, out, 256, 0.25f, 1.0f);

	assert(read > 0);

	// downsampled ramp should still be monotonically increasing
	for (uint32_t i = 1; i < read; i++) {
		assert(out[i] >= out[i - 1] - 0.01f);
	}

	resampler_free(&r);
}

static void test_read_gain_mono(void) {
	// gain=2 output should be 2x gain=1 output
	float samples[1024];
	generate_ramp(samples, 1024, -0.5f, 0.5f);

	// run with gain=1
	resampler_t *r1 = resampler_new(1, BUF_SIZE);
	resampler_enqueue(r1, samples, 1024, 1);
	float out1[256];
	uint32_t n1 = resampler_read(r1, out1, 256, 1.0f, 1.0f);
	assert(n1 > 0);

	// run with gain=2 (fresh resampler, same data)
	resampler_t *r2 = resampler_new(1, BUF_SIZE);
	resampler_enqueue(r2, samples, 1024, 1);
	float out2[256];
	uint32_t n2 = resampler_read(r2, out2, 256, 1.0f, 2.0f);
	assert(n2 > 0);

	uint32_t min_n = n1 < n2 ? n1 : n2;
	for (uint32_t i = 0; i < min_n; i++) {
		float diff = fabsf(out2[i] - out1[i] * 2.0f);
		assert(diff < 0.01f);
	}

	resampler_free(&r1);
	resampler_free(&r2);
}

// ---- resample tests - stereo ----

static void test_read_unity_ratio_stereo(void) {
	resampler_t *r = resampler_new(2, BUF_SIZE);

	// left=ramp up, right=ramp down
	float samples[2048];
	for (uint32_t i = 0; i < 1024; i++) {
		samples[i * 2]     = -0.5f + 1.0f * (float)i / 1023.0f;
		samples[i * 2 + 1] = 0.5f - 1.0f * (float)i / 1023.0f;
	}
	resampler_enqueue(r, samples, 1024, 2);

	float out[1024];
	uint32_t read = resampler_read(r, out, 512, 1.0f, 1.0f);
	assert(read > 0);

	// left channel (even indices) should increase
	// right channel (odd indices) should decrease
	for (uint32_t i = 1; i < read; i++) {
		assert(out[i * 2] >= out[(i - 1) * 2] - 0.001f);        // left increasing
		assert(out[i * 2 + 1] <= out[(i - 1) * 2 + 1] + 0.001f); // right decreasing
	}

	resampler_free(&r);
}

static void test_read_upsample_stereo(void) {
	resampler_t *r = resampler_new(2, BUF_SIZE);

	float samples[2048];
	for (uint32_t i = 0; i < 1024; i++) {
		samples[i * 2]     = -0.5f + 1.0f * (float)i / 1023.0f;
		samples[i * 2 + 1] = 0.5f - 1.0f * (float)i / 1023.0f;
	}
	resampler_enqueue(r, samples, 1024, 2);

	// 2x upsample
	float out[1024];
	uint32_t read = resampler_read(r, out, 512, 2.0f, 1.0f);
	assert(read > 0);

	// check monotonicity for both channels
	for (uint32_t i = 1; i < read; i++) {
		assert(out[i * 2] >= out[(i - 1) * 2] - 0.001f);
		assert(out[i * 2 + 1] <= out[(i - 1) * 2 + 1] + 0.001f);
	}

	resampler_free(&r);
}

// ---- channel isolation tests ----

static void test_channel_isolation_stereo(void) {
	// verify no crosstalk between channels
	resampler_t *r = resampler_new(2, BUF_SIZE);

	// left=sine, right=DC
	float samples[2048];
	for (uint32_t i = 0; i < 1024; i++) {
		float t = (float)i / (float)SAMPLE_RATE;
		samples[i * 2]     = 0.5f * sinf(2.0f * (float)M_PI * 1000.0f * t);
		samples[i * 2 + 1] = 0.9f;  // constant
	}
	resampler_enqueue(r, samples, 1024, 2);

	float out[1024];
	uint32_t read = resampler_read(r, out, 512, 1.0f, 1.0f);
	assert(read > 100);

	// right channel should stay near 0.9 (no sine leakage)
	for (uint32_t i = 50; i < read - 50; i++) {
		assert(fabsf(out[i * 2 + 1] - 0.9f) < 0.05f);
	}

	// left channel should oscillate (not constant)
	float left_min = 1.0f, left_max = -1.0f;
	for (uint32_t i = 50; i < read - 50; i++) {
		float v = out[i * 2];
		if (v < left_min) left_min = v;
		if (v > left_max) left_max = v;
	}
	assert(left_max - left_min > 0.5f);  // should have significant variation

	resampler_free(&r);
}

static void test_channel_isolation_quad(void) {
	// 4 channels, each with distinct DC value
	resampler_t *r = resampler_new(4, BUF_SIZE);

	float samples[2048];
	for (uint32_t i = 0; i < 512; i++) {
		samples[i * 4 + 0] = 0.1f;
		samples[i * 4 + 1] = 0.3f;
		samples[i * 4 + 2] = 0.5f;
		samples[i * 4 + 3] = 0.7f;
	}
	resampler_enqueue(r, samples, 512, 4);

	float out[1024];
	uint32_t read = resampler_read(r, out, 256, 1.0f, 1.0f);
	assert(read > 100);

	// each channel should maintain its DC value
	for (uint32_t i = 50; i < read - 50; i++) {
		assert(fabsf(out[i * 4 + 0] - 0.1f) < 0.02f);
		assert(fabsf(out[i * 4 + 1] - 0.3f) < 0.02f);
		assert(fabsf(out[i * 4 + 2] - 0.5f) < 0.02f);
		assert(fabsf(out[i * 4 + 3] - 0.7f) < 0.02f);
	}

	resampler_free(&r);
}

static void test_channel_independence(void) {
	// each channel gets a different frequency sine wave
	resampler_t *r = resampler_new(3, BUF_SIZE);

	float samples[3072];  // 1024 frames * 3 channels
	for (uint32_t i = 0; i < 1024; i++) {
		float t = (float)i / (float)SAMPLE_RATE;
		samples[i * 3 + 0] = 0.4f * sinf(2.0f * (float)M_PI * 500.0f * t);
		samples[i * 3 + 1] = 0.4f * sinf(2.0f * (float)M_PI * 1000.0f * t);
		samples[i * 3 + 2] = 0.4f * sinf(2.0f * (float)M_PI * 2000.0f * t);
	}
	resampler_enqueue(r, samples, 1024, 3);

	float out[1536];
	uint32_t read = resampler_read(r, out, 512, 1.0f, 1.0f);
	assert(read > 100);

	// each channel should have appropriate oscillation frequency
	// (rough check: count zero crossings)
	uint32_t crossings[3] = {0, 0, 0};
	for (uint32_t i = 1; i < read; i++) {
		for (uint32_t c = 0; c < 3; c++) {
			float prev = out[(i - 1) * 3 + c];
			float curr = out[i * 3 + c];
			if ((prev < 0.0f && curr >= 0.0f) || (prev >= 0.0f && curr < 0.0f)) {
				crossings[c]++;
			}
		}
	}

	// ch1 (1000Hz) should have ~2x crossings of ch0 (500Hz)
	// ch2 (2000Hz) should have ~2x crossings of ch1 (1000Hz)
	// (approximate check)
	assert(crossings[1] > crossings[0]);
	assert(crossings[2] > crossings[1]);

	resampler_free(&r);
}

// ---- signal integrity tests ----

static void test_dc_passthrough_mono(void) {
	resampler_t *r = resampler_new(1, BUF_SIZE);

	float samples[1024];
	generate_dc(samples, 1024, 0.5f);
	resampler_enqueue(r, samples, 1024, 1);

	float out[256];
	uint32_t read = resampler_read(r, out, 256, 1.0f, 1.0f);
	assert(read > 100);

	// tail of output should converge to 0.5
	for (uint32_t i = read - 16; i < read; i++) {
		assert(fabsf(out[i] - 0.5f) < 0.05f);
	}

	resampler_free(&r);
}

static void test_sinc_interpolation_accuracy(void) {
	// band-limited sine should be reproduced accurately at 1:1 ratio
	resampler_t *r = resampler_new(1, BUF_SIZE);

	float samples[2048];
	generate_sine(samples, 2048, 1000.0f, 0.5f, SAMPLE_RATE);
	resampler_enqueue(r, samples, 2048, 1);

	float out[512];
	uint32_t read = resampler_read(r, out, 512, 1.0f, 1.0f);
	assert(read > 100);

	// all values should stay within [-0.5, 0.5] plus small margin for Gibbs
	for (uint32_t i = 0; i < read; i++) {
		assert(out[i] >= -0.6f && out[i] <= 0.6f);
	}

	resampler_free(&r);
}

// ---- extreme ratio tests ----

static void test_extreme_upsample(void) {
	resampler_t *r = resampler_new(1, BUF_SIZE);

	float samples[256];
	generate_ramp(samples, 256, -0.5f, 0.5f);
	resampler_enqueue(r, samples, 256, 1);

	// 16x upsample
	float out[512];
	uint32_t read = resampler_read(r, out, 512, 16.0f, 1.0f);

	assert(read > 0);

	// should still be monotonically increasing
	for (uint32_t i = 1; i < read; i++) {
		assert(out[i] >= out[i - 1] - 0.001f);
	}

	resampler_free(&r);
}

static void test_extreme_downsample(void) {
	resampler_t *r = resampler_new(1, BUF_SIZE);

	float samples[2048];
	generate_ramp(samples, 2048, -0.5f, 0.5f);
	resampler_enqueue(r, samples, 2048, 1);

	// 16x downsample
	float out[64];
	uint32_t read = resampler_read(r, out, 64, 0.0625f, 1.0f);

	assert(read > 0);

	// output values must be valid (no buffer overrun)
	for (uint32_t i = 0; i < read; i++) {
		assert(out[i] >= -1.0f && out[i] <= 1.0f);
	}

	resampler_free(&r);
}

static void test_fractional_ratio(void) {
	// non-integer, non-power-of-2 ratio
	resampler_t *r = resampler_new(1, BUF_SIZE);

	float samples[1024];
	generate_ramp(samples, 1024, -0.5f, 0.5f);
	resampler_enqueue(r, samples, 1024, 1);

	// 1.7x upsample
	float out[512];
	uint32_t read = resampler_read(r, out, 512, 1.7f, 1.0f);

	assert(read > 0);

	// should still be monotonically increasing
	for (uint32_t i = 1; i < read; i++) {
		assert(out[i] >= out[i - 1] - 0.001f);
	}

	resampler_free(&r);
}

// ---- cross-call continuity ----

static void test_read_continuity_across_calls(void) {
	// When read drains the buffer and then resumes after more data
	// is enqueued, the output waveform must be continuous
	resampler_t *r = resampler_new(1, BUF_SIZE);

	float samples[512];
	generate_sine(samples, 512, 100.0f, 0.5f, SAMPLE_RATE);
	resampler_enqueue(r, samples, 512, 1);

	// drain buffer
	float out[1024];
	float last_value = 0.0f;
	uint32_t total_read = 0;

	while (1) {
		uint32_t n = resampler_read(r, out, 1024, 1.0f, 1.0f);
		if (n == 0) break;
		last_value = out[n - 1];
		total_read += n;
	}
	assert(total_read > 0);

	// enqueue more data
	generate_sine(samples, 512, 100.0f, 0.5f, SAMPLE_RATE);
	resampler_enqueue(r, samples, 512, 1);

	// read more
	uint32_t n = resampler_read(r, out, 1024, 1.0f, 1.0f);
	assert(n > 0);

	// first value should be continuous with last value
	// for 100Hz sine at 48kHz: max change per sample = 0.5 * 2*pi*100/48000 ≈ 0.00654
	float discontinuity = fabsf(out[0] - last_value);
	assert(discontinuity < 0.02f);

	resampler_free(&r);
}

static void test_position_preservation(void) {
	// fractional position should be preserved across multiple read calls
	resampler_t *r = resampler_new(1, BUF_SIZE);

	// enqueue continuous ramp
	float samples[2048];
	generate_ramp(samples, 2048, -1.0f, 1.0f);
	resampler_enqueue(r, samples, 2048, 1);

	// read in small chunks with non-integer ratio
	float out[64];
	float prev_last = -999.0f;
	int first = 1;

	for (int i = 0; i < 10; i++) {
		uint32_t n = resampler_read(r, out, 64, 1.3f, 1.0f);
		if (n == 0) break;

		if (!first) {
			// check continuity between chunks
			float gap = fabsf(out[0] - prev_last);
			assert(gap < 0.05f);
		}
		first = 0;
		prev_last = out[n - 1];
	}

	resampler_free(&r);
}

// ---- concurrent enqueue/read ----

typedef struct {
	resampler_t *resampler;
	uint32_t n_iterations;
	uint32_t channels;
	float freq;
} audio_thread_args_t;

static int audio_thread_func(void *arg) {
	audio_thread_args_t *a = (audio_thread_args_t *)arg;
	uint32_t frame_size = 256;
	float *buf = malloc(frame_size * a->channels * sizeof(float));
	assert(buf != NULL);

	for (uint32_t iter = 0; iter < a->n_iterations; iter++) {
		for (uint32_t i = 0; i < frame_size; i++) {
			float t = (float)(iter * frame_size + i) / (float)SAMPLE_RATE;
			float val = 0.5f * sinf(2.0f * (float)M_PI * a->freq * t);
			for (uint32_t c = 0; c < a->channels; c++) {
				buf[i * a->channels + c] = val;
			}
		}
		resampler_enqueue(a->resampler, buf, frame_size, a->channels);
		SDL_Delay(1);
	}

	free(buf);
	return 0;
}

static void test_concurrent_enqueue_read_mono(void) {
	resampler_t *r = resampler_new(1, BUF_SIZE);

	audio_thread_args_t args = {
		.resampler = r,
		.n_iterations = 100,
		.channels = 1,
		.freq = 1000.0f,
	};

	SDL_Thread *audio = SDL_CreateThread(audio_thread_func, "AudioThread", &args);

	float out[128];
	for (uint32_t i = 0; i < 200; i++) {
		uint32_t n = resampler_read(r, out, 128, 1.0f, 1.0f);
		for (uint32_t j = 0; j < n; j++) {
			assert(out[j] >= -1.0f && out[j] <= 1.0f);
		}
		SDL_Delay(1);
	}

	SDL_WaitThread(audio, NULL);
	resampler_free(&r);
}

static void test_concurrent_enqueue_read_stereo(void) {
	resampler_t *r = resampler_new(2, BUF_SIZE);

	audio_thread_args_t args = {
		.resampler = r,
		.n_iterations = 100,
		.channels = 2,
		.freq = 1000.0f,
	};

	SDL_Thread *audio = SDL_CreateThread(audio_thread_func, "AudioThread", &args);

	float out[256];
	for (uint32_t i = 0; i < 200; i++) {
		uint32_t n = resampler_read(r, out, 128, 1.0f, 1.0f);
		for (uint32_t j = 0; j < n * 2; j++) {
			assert(out[j] >= -1.0f && out[j] <= 1.0f);
		}
		SDL_Delay(1);
	}

	SDL_WaitThread(audio, NULL);
	resampler_free(&r);
}

// ---- anti-aliasing tests ----

static void test_downsample_antialiasing(void) {
	// high frequency component should be attenuated when downsampling
	resampler_t *r = resampler_new(1, BUF_SIZE);

	// mix 500Hz (in passband) + 10kHz (should be filtered out during 4x downsample)
	float samples[2048];
	for (uint32_t i = 0; i < 2048; i++) {
		float t = (float)i / (float)SAMPLE_RATE;
		samples[i] = 0.3f * sinf(2.0f * (float)M_PI * 500.0f * t)
		           + 0.2f * sinf(2.0f * (float)M_PI * 10000.0f * t);
	}
	resampler_enqueue(r, samples, 2048, 1);

	// 4x downsample: output rate = 12kHz, nyquist = 6kHz
	// 10kHz component should be heavily attenuated
	float out[256];
	uint32_t read = resampler_read(r, out, 256, 0.25f, 1.0f);
	assert(read > 100);

	// without anti-aliasing, amplitude would be ~0.5 (0.3 + 0.2)
	// with anti-aliasing, 10kHz component filtered out, amplitude ~0.3
	float max_amp = 0.0f;
	for (uint32_t i = 50; i < read - 50; i++) {
		if (fabsf(out[i]) > max_amp) max_amp = fabsf(out[i]);
	}

	// expect peak near 0.3, definitely not near 0.5
	assert(max_amp < 0.42f);
	assert(max_amp > 0.2f);

	resampler_free(&r);
}

// ---- multi-channel concurrent stress test ----

static void test_concurrent_multi_channel(void) {
	resampler_t *r = resampler_new(4, BUF_SIZE);

	audio_thread_args_t args = {
		.resampler = r,
		.n_iterations = 50,
		.channels = 4,
		.freq = 500.0f,
	};

	SDL_Thread *audio = SDL_CreateThread(audio_thread_func, "AudioThread", &args);

	float out[512];
	for (uint32_t i = 0; i < 100; i++) {
		uint32_t n = resampler_read(r, out, 128, 1.5f, 1.0f);
		for (uint32_t j = 0; j < n * 4; j++) {
			assert(out[j] >= -1.0f && out[j] <= 1.0f);
		}
		SDL_Delay(1);
	}

	SDL_WaitThread(audio, NULL);
	resampler_free(&r);
}

// ---- interleaved correctness ----

static void test_interleaved_ordering(void) {
	// verify samples are returned in correct interleaved order
	resampler_t *r = resampler_new(3, BUF_SIZE);

	// distinct DC value per channel
	float samples[1536];  // 512 frames * 3 channels
	for (uint32_t i = 0; i < 512; i++) {
		samples[i * 3 + 0] = 0.1f;
		samples[i * 3 + 1] = 0.5f;
		samples[i * 3 + 2] = 0.9f;
	}
	resampler_enqueue(r, samples, 512, 3);

	float out[768];
	uint32_t read = resampler_read(r, out, 256, 1.0f, 1.0f);
	assert(read > 100);

	// verify interleaving pattern
	for (uint32_t i = 50; i < read - 50; i++) {
		assert(fabsf(out[i * 3 + 0] - 0.1f) < 0.05f);
		assert(fabsf(out[i * 3 + 1] - 0.5f) < 0.05f);
		assert(fabsf(out[i * 3 + 2] - 0.9f) < 0.05f);
	}

	resampler_free(&r);
}

// ---- runner ----

#define RUN_TEST(test) do { \
	printf("  %s...", #test); \
	test(); \
	printf(" ok\n"); \
} while(0)

int main(void) {
	if (SDL_Init(SDL_INIT_TIMER) != 0) {
		fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
		return 1;
	}

	printf("resampler tests:\n");

	// lifecycle
	RUN_TEST(test_new_free);
	RUN_TEST(test_new_various_channels);
	RUN_TEST(test_new_minimum_buffer);
	RUN_TEST(test_new_buffer_too_small);
	RUN_TEST(test_new_zero_channels);
	RUN_TEST(test_free_null_safety);

	// buffer rounding
	RUN_TEST(test_buffer_rounds_to_power_of_2);

	// enqueue
	RUN_TEST(test_enqueue_mono);
	RUN_TEST(test_enqueue_stereo);
	RUN_TEST(test_enqueue_multi_channel);
	RUN_TEST(test_enqueue_overflow);
	RUN_TEST(test_enqueue_channel_mismatch);
	RUN_TEST(test_enqueue_null_guards);

	// read guards
	RUN_TEST(test_read_null_guards);
	RUN_TEST(test_read_empty_buffer);

	// resample - mono
	RUN_TEST(test_read_unity_ratio_mono);
	RUN_TEST(test_read_upsample_mono);
	RUN_TEST(test_read_downsample_mono);
	RUN_TEST(test_read_gain_mono);

	// resample - stereo
	RUN_TEST(test_read_unity_ratio_stereo);
	RUN_TEST(test_read_upsample_stereo);

	// channel isolation
	RUN_TEST(test_channel_isolation_stereo);
	RUN_TEST(test_channel_isolation_quad);
	RUN_TEST(test_channel_independence);

	// signal integrity
	RUN_TEST(test_dc_passthrough_mono);
	RUN_TEST(test_sinc_interpolation_accuracy);

	// extreme ratios
	RUN_TEST(test_extreme_upsample);
	RUN_TEST(test_extreme_downsample);
	RUN_TEST(test_fractional_ratio);

	// continuity
	RUN_TEST(test_read_continuity_across_calls);
	RUN_TEST(test_position_preservation);

	// concurrency
	RUN_TEST(test_concurrent_enqueue_read_mono);
	RUN_TEST(test_concurrent_enqueue_read_stereo);

	// anti-aliasing
	RUN_TEST(test_downsample_antialiasing);

	// stress test
	RUN_TEST(test_concurrent_multi_channel);

	// interleaved correctness
	RUN_TEST(test_interleaved_ordering);

	printf("\nAll tests passed.\n");

	SDL_Quit();
	return 0;
}
