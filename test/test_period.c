// Period estimation and conditioning, held to the validation oracle's goldens.
#include "greatest.h"
#include "period.h"
#include "fft_analyzer.h"
#include "test_signals.h"
#include "period_golden.h"

#include <stdlib.h>
#include <string.h>

// The chain from samples to (period, clarity): the analyzer's normalized
// autocorrelation, then the estimate, for each golden signal.
TEST estimates_match_golden(void) {
	for (unsigned i = 0; i < PERIOD_GOLDEN_ESTIMATE_COUNT; i++) {
		const period_golden_estimate_t *g = &period_golden_estimates[i];
		test_signal_t s = { g->signal.kind, g->signal.freq, g->signal.freq2, g->signal.amp, g->signal.phase,
							g->signal.harmonics, g->signal.seed, 0 };
		float *x = malloc(sizeof(float) * g->n);
		float *nacf = malloc(sizeof(float) * g->n);
		test_signal_generate(&s, x, g->n, PERIOD_GOLDEN_FS);

		fft_analyzer_t *fa = fft_analyzer_new(&(fft_analyzer_config_t){
			.fft_len = g->n, .window_function = (window_function_t)g->window.function,
			.window_skew = (float)g->window.skew, .mode = FFT_ANALYZER_MIX,
		});
		fft_analyzer_prepare_to_stream(fa, &(s9_audio_stream_config_t){
			.audio_format = S9_FORMAT_FLOAT32, .sample_rate = PERIOD_GOLDEN_FS, .channels = 1,
			.buffer_frames = 256, .samples_per_display_frame = g->n,
		});
		fft_analyzer_enqueue_audio_data(fa, x, sizeof(float) * g->n);
		fft_analyzer_update(fa);
		ASSERT_EQm(g->label, 0, fft_analyzer_nacf(fa, 0, nacf, g->n));

		period_estimate_t e = period_estimate(nacf, g->n, g->lag_min, g->lag_max, (float)g->tolerance);
		if (g->periodic) {
			ASSERT_IN_RANGEm(g->label, g->period, (double)e.period, g->period * 2e-5);
			ASSERT_IN_RANGEm(g->label, g->clarity, (double)e.clarity, 1e-5);
		} else {
			ASSERTm(g->label, e.clarity < 0.5f);
		}

		fft_analyzer_free(&fa);
		free(nacf);
		free(x);
	}
	PASS();
}

// Peak picking on constructed functions: tolerance, lag bounds, fractional
// refinement, a peak cut off by lag_max, and none at all.
TEST picks_match_golden(void) {
	float *nacf = malloc(sizeof(float) * PERIOD_GOLDEN_PICK_N);
	for (unsigned i = 0; i < PERIOD_GOLDEN_PICK_COUNT; i++) {
		const period_golden_pick_t *g = &period_golden_picks[i];
		for (unsigned t = 0; t < PERIOD_GOLDEN_PICK_N; t++) nacf[t] = (float)g->nacf[t];
		period_estimate_t e = period_estimate(nacf, PERIOD_GOLDEN_PICK_N, g->lag_min, g->lag_max, (float)g->tolerance);
		ASSERT_IN_RANGEm(g->label, g->period, (double)e.period, 1e-3);
		ASSERT_IN_RANGEm(g->label, g->clarity, (double)e.clarity, 1e-5);
		if (!g->periodic) ASSERT_EQm(g->label, 0.0f, e.period);
	}
	free(nacf);
	PASS();
}

// The conditioner's frame-by-frame outputs for each golden trace.
TEST conditioner_traces_match_golden(void) {
	const period_golden_conditioner_config_t *c = &period_golden_conditioner_config;
	period_conditioner_config_t config = {
		.gate_rms = (float)c->gate_rms, .clarity_on = (float)c->clarity_on, .clarity_off = (float)c->clarity_off,
		.hold_s = (float)c->hold_s, .octave_tolerance_cents = (float)c->octave_tolerance_cents,
		.smoothing_s = (float)c->smoothing_s, .deadband_cents = (float)c->deadband_cents,
	};
	period_conditioner_t *pc = malloc(period_conditioner_alloc_size());
	for (unsigned i = 0; i < PERIOD_GOLDEN_TRACE_COUNT; i++) {
		const period_golden_trace_t *trace = &period_golden_traces[i];
		period_conditioner_init(pc, &config);
		for (unsigned k = 0; k < trace->count; k++) {
			const period_golden_step_t *step = &trace->steps[k];
			float frequency;
			int valid = period_conditioner_update(pc, (float)step->period, (float)step->clarity, (float)step->rms,
												 PERIOD_GOLDEN_FS, (float)PERIOD_GOLDEN_TRACE_DT, &frequency);
			ASSERT_EQm(trace->label, step->valid, valid);
			ASSERT_IN_RANGEm(trace->label, step->frequency, (double)frequency, step->frequency * 1e-5 + 1e-6);
		}
	}
	free(pc);
	PASS();
}

// An empty or all-negative function yields no estimate.
TEST no_positive_region_yields_nothing(void) {
	float nacf[16] = { 1.0f };
	for (int i = 1; i < 16; i++) nacf[i] = -0.1f;
	period_estimate_t e = period_estimate(nacf, 16, 2, 15, 0.02f);
	ASSERT_EQ(0.0f, e.period);
	ASSERT_EQ(0.0f, e.clarity);
	e = period_estimate(nacf, 1, 0, 0, 0.02f);
	ASSERT_EQ(0.0f, e.period);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(estimates_match_golden);
	RUN_TEST(picks_match_golden);
	RUN_TEST(conditioner_traces_match_golden);
	RUN_TEST(no_positive_region_yields_nothing);
	GREATEST_MAIN_END();
}
