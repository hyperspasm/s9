// The phase-locked loop, held to the validation oracle's goldens: event
// trains that jitter, that start off the seeded period by a little and by
// a lot, that slip, that change rate, that skip events, and that are
// passed straight through.
#include "greatest.h"
#include "pll.h"
#include "pll_golden.h"

#include <math.h>
#include <stdlib.h>

// Every observation's error, the period after it, and the next event
// predicted from it match the oracle's.
TEST traces_match_golden(void) {
	pll_t *p = malloc(pll_alloc_size());
	for (unsigned i = 0; i < PLL_GOLDEN_TRACE_COUNT; i++) {
		const pll_golden_trace_t *trace = &pll_golden_traces[i];
		pll_init(p, &(pll_config_t){ .tc = trace->config.tc, .capture = trace->config.capture });
		pll_seed(p, trace->seed);
		ASSERT_FALSEm(trace->label, pll_locked(p));
		for (unsigned k = 0; k < trace->count; k++) {
			const pll_golden_step_t *step = &trace->steps[k];
			double error = pll_observe(p, step->t);
			ASSERT_IN_RANGEm(trace->label, step->error, error, 1e-9);
			ASSERT_IN_RANGEm(trace->label, step->period, pll_period(p), 1e-9);
			ASSERT_IN_RANGEm(trace->label, step->next, pll_next(p, step->t), 1e-9);
			ASSERTm(trace->label, pll_locked(p));
		}
	}
	free(p);
	PASS();
}

// Without a period there is nothing to lock to, and an unlocked clock
// predicts nothing earlier than asked.
TEST unseeded_and_unlocked(void) {
	pll_t *p = malloc(pll_alloc_size());
	pll_init(p, &(pll_config_t){ .tc = 10.0, .capture = 0.25 });
	ASSERT_EQ_FMT(0.0, pll_observe(p, 5.0), "%g");
	ASSERT_FALSE(pll_locked(p));
	ASSERT_EQ_FMT(7.0, pll_next(p, 7.0), "%g");
	pll_seed(p, 4.0);
	ASSERT_EQ_FMT(0.0, pll_observe(p, 5.0), "%g");
	ASSERT(pll_locked(p));
	ASSERT_EQ_FMT(9.0, pll_next(p, 6.0), "%g");
	ASSERT_EQ_FMT(5.0, pll_next(p, 5.0), "%g");
	pll_reset(p);
	ASSERT_FALSE(pll_locked(p));
	free(p);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(traces_match_golden);
	RUN_TEST(unseeded_and_unlocked);
	GREATEST_MAIN_END();
}
