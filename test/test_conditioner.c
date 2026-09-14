// The conditioner, held to the validation oracle's traces: the gate and its
// hysteresis, the hold, the octave guard, the smoothing with its dead band,
// and the snap to a new note.
#include "greatest.h"
#include "conditioner.h"
#include "conditioner_golden.h"

#include <stdlib.h>

static conditioner_config_t golden_config(void) {
	const conditioner_golden_config_t *c = &conditioner_golden_config;
	return (conditioner_config_t){
		.gate_rms = (float)c->gate_rms, .harmonicity_on = (float)c->harmonicity_on, .harmonicity_off = (float)c->harmonicity_off,
		.hold_s = (float)c->hold_s, .octave_tolerance_cents = (float)c->octave_tolerance_cents,
		.smoothing_s = (float)c->smoothing_s, .deadband_cents = (float)c->deadband_cents, .snap_cents = (float)c->snap_cents,
	};
}

// The frame-by-frame outputs for each golden trace.
TEST traces_match_golden(void) {
	conditioner_config_t config = golden_config();
	conditioner_t *c = malloc(conditioner_alloc_size());
	for (unsigned i = 0; i < CONDITIONER_GOLDEN_TRACE_COUNT; i++) {
		const conditioner_golden_trace_t *trace = &conditioner_golden_traces[i];
		conditioner_init(c, &config);
		for (unsigned k = 0; k < trace->count; k++) {
			const conditioner_golden_step_t *step = &trace->steps[k];
			float frequency;
			int valid = conditioner_update(c, (float)step->estimate_hz, (float)step->harmonicity, (float)step->rms,
			                               (float)CONDITIONER_GOLDEN_TRACE_DT, &frequency);
			ASSERT_EQm(trace->label, step->valid, valid);
			ASSERT_IN_RANGEm(trace->label, step->frequency, (double)frequency, step->frequency * 1e-5 + 1e-6);
		}
	}
	free(c);
	PASS();
}

// A reset forgets the fundamental: invalid, and no frequency until the next
// accepted estimate.
TEST reset_forgets(void) {
	conditioner_config_t config = golden_config();
	conditioner_t *c = malloc(conditioner_alloc_size());
	conditioner_init(c, &config);
	float frequency;
	ASSERT(conditioner_update(c, 440.0f, 0.95f, 0.1f, 0.02f, &frequency));
	ASSERT_IN_RANGE(440.0, (double)frequency, 1e-3);
	conditioner_reset(c);
	ASSERT_FALSE(conditioner_update(c, 0.0f, 0.0f, 0.1f, 0.02f, &frequency));
	ASSERT_EQ_FMT(0.0f, frequency, "%g");
	free(c);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(traces_match_golden);
	RUN_TEST(reset_forgets);
	GREATEST_MAIN_END();
}
