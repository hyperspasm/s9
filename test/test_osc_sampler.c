#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include "osc_sampler.h"

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

// ---- lifecycle tests ----

static void test_new_free(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);
	assert(s != NULL);
	osc_sampler_free(&s);
	assert(s == NULL);
}

static void test_new_minimum_buffer(void) {
	// N=17, minimum is N+1=18. Power-of-two rounds up to 32.
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, 18);
	assert(s != NULL);
	osc_sampler_free(&s);
}

static void test_new_buffer_too_small(void) {
	// buffer_size < N+1=18 should fail
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, 17);
	assert(s == NULL);
}

static void test_free_null_safety(void) {
	osc_sampler_free(NULL);
	osc_sampler_t *s = NULL;
	osc_sampler_free(&s);
	// should not crash
}

// ---- enqueue tests ----

static void test_enqueue_mono(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);
	float samples[256];
	generate_dc(samples, 256, 0.5f);

	uint32_t enqueued = osc_sampler_enqueue(s, samples, 256, 1);
	assert(enqueued == 256);

	osc_sampler_free(&s);
}

static void test_enqueue_stereo(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	// interleaved stereo: [L0, R0, L1, R1, ...]
	float samples[512];
	for (uint32_t i = 0; i < 256; i++) {
		samples[i * 2]     = (float)i / 255.0f;  // left channel (ramp)
		samples[i * 2 + 1] = -1.0f;              // right channel (ignored)
	}

	uint32_t enqueued = osc_sampler_enqueue(s, samples, 256, 2);
	assert(enqueued == 256);

	osc_sampler_free(&s);
}

static void test_enqueue_overflow(void) {
	// buffer_size=32 (already power-of-two), usable capacity = 31
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, 32);
	float samples[64];
	generate_dc(samples, 64, 0.5f);

	uint32_t enqueued = osc_sampler_enqueue(s, samples, 64, 1);
	assert(enqueued <= 31);

	osc_sampler_free(&s);
}

static void test_enqueue_null_guards(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);
	float samples[16] = {0};

	assert(osc_sampler_enqueue(NULL, samples, 16, 1) == 0);
	assert(osc_sampler_enqueue(s, NULL, 16, 1) == 0);
	assert(osc_sampler_enqueue(s, samples, 0, 1) == 0);

	osc_sampler_free(&s);
}

// ---- read_pixels guards ----

static void test_read_pixels_null_guards(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);
	float out[16] = {0};

	assert(osc_sampler_read_pixels(NULL, out, 16, 1000.0f, 1.0f, NULL) == 0);
	assert(osc_sampler_read_pixels(s, NULL, 16, 1000.0f, 1.0f, NULL) == 0);
	assert(osc_sampler_read_pixels(s, out, 16, 0.0f, 1.0f, NULL) == 0);
	assert(osc_sampler_read_pixels(s, out, 16, -1.0f, 1.0f, NULL) == 0);

	osc_sampler_free(&s);
}

// ---- trigger tests ----

static void test_trigger_rising_edge(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	// ramp from -0.5 to 0.5 crosses default threshold 0.1
	float samples[512];
	generate_ramp(samples, 512, -0.5f, 0.5f);
	osc_sampler_enqueue(s, samples, 512, 1);

	float out[256];
	int triggered = 0;
	uint32_t pixels = osc_sampler_read_pixels(s, out, 256, (float)SAMPLE_RATE, 1.0f, &triggered);

	assert(triggered == 1);
	assert(pixels > 0);

	osc_sampler_free(&s);
}

static void test_trigger_falling_edge(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	osc_sampler_settings_t settings = {
		.trigger_mode = osc_trig_mode_normal,
		.trig_slope = -1,
		.trig_thresh = 0.0f,
		.trig_holdoff = 0.00001f,
	};
	osc_sampler_set_settings(s, &settings);

	// ramp from 0.5 to -0.5 crosses threshold 0.0 going down
	float samples[512];
	generate_ramp(samples, 512, 0.5f, -0.5f);
	osc_sampler_enqueue(s, samples, 512, 1);

	float out[256];
	int triggered = 0;
	uint32_t pixels = osc_sampler_read_pixels(s, out, 256, (float)SAMPLE_RATE, 1.0f, &triggered);

	assert(triggered == 1);
	assert(pixels > 0);

	osc_sampler_free(&s);
}

static void test_no_trigger_dc_above(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	// DC above threshold: pre_trigger never arms (needs signal below threshold)
	float samples[512];
	generate_dc(samples, 512, 0.5f);
	osc_sampler_enqueue(s, samples, 512, 1);

	float out[256];
	int triggered = 0;
	uint32_t pixels = osc_sampler_read_pixels(s, out, 256, (float)SAMPLE_RATE, 1.0f, &triggered);

	assert(triggered == 0);
	assert(pixels == 0);

	osc_sampler_free(&s);
}

static void test_no_trigger_dc_below(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	// DC below threshold: pre_trigger arms, but no crossing ever happens
	float samples[512];
	generate_dc(samples, 512, -0.5f);
	osc_sampler_enqueue(s, samples, 512, 1);

	float out[256];
	int triggered = 0;
	uint32_t pixels = osc_sampler_read_pixels(s, out, 256, (float)SAMPLE_RATE, 1.0f, &triggered);

	assert(triggered == 0);
	assert(pixels == 0);

	osc_sampler_free(&s);
}

static void test_no_trigger_empty_buffer(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	float out[256];
	int triggered = 0;
	uint32_t pixels = osc_sampler_read_pixels(s, out, 256, (float)SAMPLE_RATE, 1.0f, &triggered);

	assert(triggered == 0);
	assert(pixels == 0);

	osc_sampler_free(&s);
}

static void test_trigger_threshold_accuracy(void) {
	// Verify trigger fires near the threshold crossing, not far from it
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	osc_sampler_settings_t settings = {
		.trigger_mode = osc_trig_mode_normal,
		.trig_slope = 1,
		.trig_thresh = 0.0f,
		.trig_holdoff = 0.00001f,
	};
	osc_sampler_set_settings(s, &settings);

	// ramp from -1 to 1: threshold 0.0 is at the midpoint
	float samples[2048];
	generate_ramp(samples, 2048, -1.0f, 1.0f);
	osc_sampler_enqueue(s, samples, 2048, 1);

	float out[512];
	int triggered = 0;
	uint32_t pixels = osc_sampler_read_pixels(s, out, 512, (float)SAMPLE_RATE, 1.0f, &triggered);

	assert(triggered == 1);
	assert(pixels > 0);

	// first output sample should be near threshold 0.0 (within a few samples' worth)
	assert(fabsf(out[0]) < 0.05f);

	osc_sampler_free(&s);
}

// ---- resample/read tests ----

static void test_read_unity_ratio(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	float samples[1024];
	generate_ramp(samples, 1024, -0.5f, 0.5f);
	osc_sampler_enqueue(s, samples, 1024, 1);

	float out[512];
	int triggered = 0;
	uint32_t pixels = osc_sampler_read_pixels(s, out, 512, (float)SAMPLE_RATE, 1.0f, &triggered);

	assert(triggered == 1);
	assert(pixels > 0);

	// at 1:1 ratio, output of a ramp should be monotonically increasing
	for (uint32_t i = 1; i < pixels; i++) {
		assert(out[i] >= out[i - 1] - 0.001f);
	}

	osc_sampler_free(&s);
}

static void test_read_upsample(void) {
	// pixel_rate > sample_rate: upsampling
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	float samples[1024];
	generate_ramp(samples, 1024, -0.5f, 0.5f);
	osc_sampler_enqueue(s, samples, 1024, 1);

	// 2x upsample
	float out[512];
	int triggered = 0;
	uint32_t pixels = osc_sampler_read_pixels(s, out, 512, (float)SAMPLE_RATE * 2.0f, 1.0f, &triggered);

	assert(triggered == 1);
	assert(pixels > 0);

	// upsampled ramp should still be monotonically increasing
	for (uint32_t i = 1; i < pixels; i++) {
		assert(out[i] >= out[i - 1] - 0.001f);
	}

	osc_sampler_free(&s);
}

static void test_read_downsample(void) {
	// pixel_rate < sample_rate: downsampling
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	float samples[2048];
	generate_ramp(samples, 2048, -0.5f, 0.5f);
	osc_sampler_enqueue(s, samples, 2048, 1);

	// 4x downsample
	float out[256];
	int triggered = 0;
	uint32_t pixels = osc_sampler_read_pixels(s, out, 256, (float)SAMPLE_RATE / 4.0f, 1.0f, &triggered);

	assert(triggered == 1);
	assert(pixels > 0);

	// downsampled ramp should still be monotonically increasing
	for (uint32_t i = 1; i < pixels; i++) {
		assert(out[i] >= out[i - 1] - 0.01f);
	}

	osc_sampler_free(&s);
}

static void test_read_gain(void) {
	// gain=2 output should be 2x gain=1 output
	float samples[1024];
	generate_ramp(samples, 1024, -0.5f, 0.5f);

	// run with gain=1
	osc_sampler_t *s1 = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);
	osc_sampler_enqueue(s1, samples, 1024, 1);
	float out1[256];
	int triggered = 0;
	uint32_t p1 = osc_sampler_read_pixels(s1, out1, 256, (float)SAMPLE_RATE, 1.0f, &triggered);
	assert(triggered == 1);
	assert(p1 > 0);

	// run with gain=2 (fresh sampler, same data)
	osc_sampler_t *s2 = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);
	osc_sampler_enqueue(s2, samples, 1024, 1);
	float out2[256];
	uint32_t p2 = osc_sampler_read_pixels(s2, out2, 256, (float)SAMPLE_RATE, 2.0f, &triggered);
	assert(p2 > 0);

	uint32_t min_p = p1 < p2 ? p1 : p2;
	for (uint32_t i = 0; i < min_p; i++) {
		float diff = fabsf(out2[i] - out1[i] * 2.0f);
		assert(diff < 0.01f);
	}

	osc_sampler_free(&s1);
	osc_sampler_free(&s2);
}

// ---- sweep lifecycle tests ----

static void test_sweep_complete_retrigger(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	// sine wave for multiple trigger opportunities
	float samples[3000];
	generate_sine(samples, 3000, 100.0f, 0.5f, SAMPLE_RATE);
	osc_sampler_enqueue(s, samples, 3000, 1);

	// first trigger
	float out[128];
	int triggered = 0;
	uint32_t pixels = osc_sampler_read_pixels(s, out, 128, (float)SAMPLE_RATE, 1.0f, &triggered);
	assert(triggered == 1);
	assert(pixels > 0);

	// complete sweep
	osc_sampler_sweep_complete(s);

	// feed more data and try to trigger again
	generate_sine(samples, 3000, 100.0f, 0.5f, SAMPLE_RATE);
	osc_sampler_enqueue(s, samples, 3000, 1);

	triggered = 0;
	pixels = osc_sampler_read_pixels(s, out, 128, (float)SAMPLE_RATE, 1.0f, &triggered);

	// holdoff is very short (10us ~ 0.48 samples), should re-trigger
	assert(triggered == 1);
	assert(pixels > 0);

	osc_sampler_free(&s);
}

static void test_sweep_complete_holdoff(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	// long holdoff: 100ms = 4800 samples at 48kHz
	osc_sampler_settings_t settings = {
		.trigger_mode = osc_trig_mode_normal,
		.trig_slope = 1,
		.trig_thresh = 0.1f,
		.trig_holdoff = 0.1f,
	};
	osc_sampler_set_settings(s, &settings);

	// trigger with a ramp
	float samples[1024];
	generate_ramp(samples, 1024, -0.5f, 0.5f);
	osc_sampler_enqueue(s, samples, 1024, 1);

	float out[128];
	int triggered = 0;
	osc_sampler_read_pixels(s, out, 128, (float)SAMPLE_RATE, 1.0f, &triggered);
	assert(triggered == 1);

	// complete sweep, start holdoff (4800 samples)
	osc_sampler_sweep_complete(s);

	// feed only 256 samples - not enough to exhaust 4800-sample holdoff
	generate_ramp(samples, 256, -0.5f, 0.5f);
	osc_sampler_enqueue(s, samples, 256, 1);

	triggered = 0;
	uint32_t pixels = osc_sampler_read_pixels(s, out, 128, (float)SAMPLE_RATE, 1.0f, &triggered);
	assert(triggered == 0);
	assert(pixels == 0);

	osc_sampler_free(&s);
}

// ---- signal integrity tests ----

static void test_dc_passthrough(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	// start below threshold to arm, ramp through to trigger, then hold DC 0.5
	float samples[1024];
	generate_dc(samples, 256, -0.5f);
	generate_ramp(samples + 256, 32, -0.5f, 0.5f);
	generate_dc(samples + 288, 1024 - 288, 0.5f);

	osc_sampler_enqueue(s, samples, 1024, 1);

	float out[256];
	int triggered = 0;
	uint32_t pixels = osc_sampler_read_pixels(s, out, 256, (float)SAMPLE_RATE, 1.0f, &triggered);

	assert(triggered == 1);
	assert(pixels > 0);

	// tail of output should converge to 0.5
	if (pixels > 32) {
		for (uint32_t i = pixels - 16; i < pixels; i++) {
			assert(fabsf(out[i] - 0.5f) < 0.05f);
		}
	}

	osc_sampler_free(&s);
}

static void test_sinc_interpolation_accuracy(void) {
	// band-limited sine should be reproduced accurately at 1:1 ratio
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	float samples[2048];
	generate_sine(samples, 2048, 1000.0f, 0.5f, SAMPLE_RATE);
	osc_sampler_enqueue(s, samples, 2048, 1);

	float out[512];
	int triggered = 0;
	uint32_t pixels = osc_sampler_read_pixels(s, out, 512, (float)SAMPLE_RATE, 1.0f, &triggered);

	assert(triggered == 1);
	assert(pixels > 0);

	// all values should stay within [-0.5, 0.5] plus small margin for Gibbs
	for (uint32_t i = 0; i < pixels; i++) {
		assert(out[i] >= -0.6f && out[i] <= 0.6f);
	}

	osc_sampler_free(&s);
}

// ---- regression: downsample buffer overrun ----

static void test_downsample_no_overrun(void) {
	// large increment (downsampling) must not overrun circular buffer
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	float samples[2048];
	generate_ramp(samples, 2048, -0.5f, 0.5f);
	osc_sampler_enqueue(s, samples, 2048, 1);

	float out[256];
	int triggered = 0;

	// pixel_rate=800 at 48kHz → ratio=1/60 → increment=60 samples/pixel
	uint32_t pixels = osc_sampler_read_pixels(s, out, 256, 800.0f, 1.0f, &triggered);

	assert(triggered == 1);
	assert(pixels <= 256);

	// output must be valid values, not garbage from buffer overrun
	for (uint32_t i = 0; i < pixels; i++) {
		assert(out[i] >= -1.0f && out[i] <= 1.0f);
	}

	osc_sampler_free(&s);
}

static void test_extreme_downsample(void) {
	// increment >> available data: should stop gracefully
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	float samples[512];
	generate_ramp(samples, 512, -0.5f, 0.5f);
	osc_sampler_enqueue(s, samples, 512, 1);

	float out[256];
	int triggered = 0;

	// pixel_rate=100 at 48kHz → increment=480 samples/pixel
	// with ~512 samples total, at most 1 pixel possible after trigger overhead
	uint32_t pixels = osc_sampler_read_pixels(s, out, 256, 100.0f, 1.0f, &triggered);

	// must not crash; output values must be valid
	for (uint32_t i = 0; i < pixels; i++) {
		assert(out[i] >= -1.0f && out[i] <= 1.0f);
	}

	osc_sampler_free(&s);
}

// ---- concurrent enqueue/read ----

typedef struct {
	osc_sampler_t *sampler;
	uint32_t n_iterations;
	float freq;
} audio_thread_args_t;

static int audio_thread_func(void *arg) {
	audio_thread_args_t *a = (audio_thread_args_t *)arg;
	float buf[256];

	for (uint32_t iter = 0; iter < a->n_iterations; iter++) {
		for (uint32_t i = 0; i < 256; i++) {
			float t = (float)(iter * 256 + i) / (float)SAMPLE_RATE;
			buf[i] = 0.5f * sinf(2.0f * (float)M_PI * a->freq * t);
		}
		osc_sampler_enqueue(a->sampler, buf, 256, 1);
		SDL_Delay(1);
	}
	return 0;
}

static void test_concurrent_enqueue_read(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	audio_thread_args_t args = {
		.sampler = s,
		.n_iterations = 100,
		.freq = 1000.0f,
	};

	SDL_Thread *audio = SDL_CreateThread(audio_thread_func, "AudioThread", &args);

	float out[128];
	for (uint32_t i = 0; i < 200; i++) {
		int triggered = 0;
		uint32_t pixels = osc_sampler_read_pixels(s, out, 128, (float)SAMPLE_RATE, 1.0f, &triggered);

		if (triggered && pixels > 0) {
			for (uint32_t j = 0; j < pixels; j++) {
				assert(out[j] >= -1.0f && out[j] <= 1.0f);
			}
			osc_sampler_sweep_complete(s);
		}
		SDL_Delay(1);
	}

	SDL_WaitThread(audio, NULL);
	osc_sampler_free(&s);
}

// ---- settings / null guard tests ----

static void test_set_settings_null_guards(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);
	osc_sampler_settings_t settings = {
		.trigger_mode = osc_trig_mode_normal,
		.trig_slope = 1,
		.trig_thresh = 0.5f,
		.trig_holdoff = 0.001f,
	};

	osc_sampler_set_settings(NULL, &settings);
	osc_sampler_set_settings(s, NULL);
	osc_sampler_set_settings(s, &settings);

	osc_sampler_free(&s);
}

static void test_sweep_complete_null_guard(void) {
	osc_sampler_sweep_complete(NULL);
}

// ---- channel extraction ----

static void test_stereo_channel_extraction(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	osc_sampler_settings_t settings = {
		.trigger_mode = osc_trig_mode_normal,
		.trig_slope = 1,
		.trig_thresh = 0.0f,
		.trig_holdoff = 0.00001f,
	};
	osc_sampler_set_settings(s, &settings);

	// stereo: left=ramp(-0.5 to 0.5), right=constant 0.9
	uint32_t n = 1024;
	float *interleaved = malloc(n * 2 * sizeof(float));
	assert(interleaved != NULL);
	for (uint32_t i = 0; i < n; i++) {
		interleaved[i * 2]     = -0.5f + 1.0f * (float)i / (float)(n - 1);
		interleaved[i * 2 + 1] = 0.9f;
	}

	osc_sampler_enqueue(s, interleaved, n, 2);

	float out[256];
	int triggered = 0;
	uint32_t pixels = osc_sampler_read_pixels(s, out, 256, (float)SAMPLE_RATE, 1.0f, &triggered);

	assert(triggered == 1);
	assert(pixels > 0);

	// if left channel extracted correctly, early output should start near 0
	// if right channel leaked through, values would be ~0.9
	int any_below_half = 0;
	for (uint32_t i = 0; i < pixels && i < 10; i++) {
		if (out[i] < 0.5f) any_below_half = 1;
	}
	assert(any_below_half);

	free(interleaved);
	osc_sampler_free(&s);
}

// ---- multiple sweeps ----

static void test_multiple_sweep_cycles(void) {
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	uint32_t sweep_count = 0;

	for (uint32_t cycle = 0; cycle < 5; cycle++) {
		float samples[1024];
		generate_ramp(samples, 1024, -0.5f, 0.5f);
		osc_sampler_enqueue(s, samples, 1024, 1);

		float out[128];
		int triggered = 0;
		uint32_t pixels = osc_sampler_read_pixels(s, out, 128, (float)SAMPLE_RATE, 1.0f, &triggered);

		if (triggered && pixels > 0) {
			sweep_count++;
			osc_sampler_sweep_complete(s);
		}
	}

	// with default 10us holdoff, should trigger on most cycles
	assert(sweep_count >= 3);

	osc_sampler_free(&s);
}

// ---- triggered output pointer ----

static void test_triggered_output_optional(void) {
	// triggered pointer is optional (can be NULL)
	osc_sampler_t *s = osc_sampler_new(SAMPLE_RATE, BUF_SIZE);

	float samples[512];
	generate_ramp(samples, 512, -0.5f, 0.5f);
	osc_sampler_enqueue(s, samples, 512, 1);

	float out[128];
	uint32_t pixels = osc_sampler_read_pixels(s, out, 128, (float)SAMPLE_RATE, 1.0f, NULL);
	assert(pixels > 0);

	osc_sampler_free(&s);
}

// ---- regression: cross-call continuity ----

static void test_read_continuity_across_calls(void) {
	// When read_pixels drains the buffer and then resumes after more data
	// is enqueued, the output waveform must be continuous (no discontinuity
	// at the boundary between the two calls).
	uint32_t sr = 44100;
	float pixel_rate = 4800.0f;
	float freq = 50.0f;
	float amplitude = 0.5f;
	uint32_t chunk = 4410;  // 100ms of audio

	osc_sampler_t *s = osc_sampler_new(sr, sr);

	// threshold=0 for reliable sine triggering
	osc_sampler_settings_t settings = {
		.trigger_mode = osc_trig_mode_normal,
		.trig_slope = 1,
		.trig_thresh = 0.0f,
		.trig_holdoff = 0.00001f,
	};
	osc_sampler_set_settings(s, &settings);

	float *sine = malloc(chunk * sizeof(float));
	assert(sine != NULL);
	uint32_t total_generated = 0;

	// generate and enqueue first chunk of continuous sine
	for (uint32_t i = 0; i < chunk; i++) {
		sine[i] = amplitude * sinf(2.0f * (float)M_PI * freq * (float)(total_generated + i) / (float)sr);
	}
	osc_sampler_enqueue(s, sine, chunk, 1);
	total_generated += chunk;

	// read pixels until buffer is drained
	float out[1024];
	float last_value = 0.0f;
	uint32_t total_pixels = 0;
	int triggered = 0;

	while (1) {
		uint32_t px = osc_sampler_read_pixels(s, out, 1024, pixel_rate, 1.0f, &triggered);
		if (px == 0) break;
		last_value = out[px - 1];
		total_pixels += px;
	}
	assert(total_pixels > 0);

	// generate and enqueue second chunk (continuous with first)
	for (uint32_t i = 0; i < chunk; i++) {
		sine[i] = amplitude * sinf(2.0f * (float)M_PI * freq * (float)(total_generated + i) / (float)sr);
	}
	osc_sampler_enqueue(s, sine, chunk, 1);
	total_generated += chunk;

	// read more pixels
	uint32_t px = osc_sampler_read_pixels(s, out, 1024, pixel_rate, 1.0f, &triggered);
	assert(px > 0);

	// the first pixel after resuming must be continuous with the last pixel before drain.
	// for a 50 Hz sine at 4800 px/s, max per-pixel change is:
	//   amplitude * 2*pi*freq / pixel_rate = 0.5 * 314.16 / 4800 ≈ 0.0327
	float discontinuity = fabsf(out[0] - last_value);
	float max_expected_change = 0.05f;  // generous tolerance

	printf(" [gap=%.6f max=%.6f]", discontinuity, max_expected_change);
	assert(discontinuity < max_expected_change);

	free(sine);
	osc_sampler_free(&s);
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

	printf("osc_sampler tests:\n");

	// lifecycle
	RUN_TEST(test_new_free);
	RUN_TEST(test_new_minimum_buffer);
	RUN_TEST(test_new_buffer_too_small);
	RUN_TEST(test_free_null_safety);

	// enqueue
	RUN_TEST(test_enqueue_mono);
	RUN_TEST(test_enqueue_stereo);
	RUN_TEST(test_enqueue_overflow);
	RUN_TEST(test_enqueue_null_guards);

	// read_pixels guards
	RUN_TEST(test_read_pixels_null_guards);

	// trigger
	RUN_TEST(test_trigger_rising_edge);
	RUN_TEST(test_trigger_falling_edge);
	RUN_TEST(test_no_trigger_dc_above);
	RUN_TEST(test_no_trigger_dc_below);
	RUN_TEST(test_no_trigger_empty_buffer);
	RUN_TEST(test_trigger_threshold_accuracy);

	// resample/read
	RUN_TEST(test_read_unity_ratio);
	RUN_TEST(test_read_upsample);
	RUN_TEST(test_read_downsample);
	RUN_TEST(test_read_gain);

	// sweep lifecycle
	RUN_TEST(test_sweep_complete_retrigger);
	RUN_TEST(test_sweep_complete_holdoff);

	// signal integrity
	RUN_TEST(test_dc_passthrough);
	RUN_TEST(test_sinc_interpolation_accuracy);

	// regression: buffer overrun fix
	RUN_TEST(test_downsample_no_overrun);
	RUN_TEST(test_extreme_downsample);

	// concurrency
	RUN_TEST(test_concurrent_enqueue_read);

	// null guards
	RUN_TEST(test_set_settings_null_guards);
	RUN_TEST(test_sweep_complete_null_guard);

	// channel extraction
	RUN_TEST(test_stereo_channel_extraction);

	// multi-sweep
	RUN_TEST(test_multiple_sweep_cycles);
	RUN_TEST(test_triggered_output_optional);

	// cross-call continuity
	RUN_TEST(test_read_continuity_across_calls);

	printf("\nAll tests passed.\n");

	SDL_Quit();
	return 0;
}
