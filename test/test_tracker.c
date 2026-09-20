// The two-stage tracker, held to the promises of validation/tracker/objective.md
// on a few signals: a sawtooth locks and commits within the lock latency and reads
// within a few cents, a sub-oscillator's series is read at the sub, a sine after a
// sine a twelfth below is read as itself, silence ends the lock after the hold, and
// noise never commits.
#include "greatest.h"
#include "tracker.h"
#include "test_signals.h"

#include <math.h>
#include <stdlib.h>

#define FS 48000
#define HOP 480

static double cents_from(double f, double ref) { return 1200.0 * log2(f / ref); }

// feed a signal hop by hop; the time, in seconds of audio, at which the fundamental
// first reads within tolerance cents of ref, or -1
static double time_to_read(tracker_t *t, const float *x, uint32_t n, double ref, double tolerance) {
	for (uint32_t at = 0; at + HOP <= n; at += HOP) {
		tracker_feed(t, x + at, HOP);
		tracker_state_t s = tracker_state(t);
		if (s.committed && fabs(cents_from(s.fundamental_hz, ref)) <= tolerance) return (double)(at + HOP) / FS;
	}
	return -1.0;
}

TEST a_sawtooth_locks_and_reads_within_the_promise(void) {
	uint32_t n = 2 * FS;
	float *x = calloc(n, sizeof(float));
	test_signal_t s = { SIGNAL_SAWTOOTH, 220.0, 0.0, 0.5, 0.0, 40, 1, 0.0 };
	test_signal_generate(&s, x + FS / 10, n - FS / 10, FS);   // silence for 100 ms, then the note
	tracker_t *t = tracker_new(NULL, FS);
	ASSERT(t);
	double at = time_to_read(t, x, n, 220.0, 5.0);
	ASSERT(at > 0.0);
	ASSERT(at - 0.1 <= 0.15);   // within the lock latency of the onset
	tracker_state_t st = tracker_state(t);
	ASSERT(st.locked && st.committed);
	ASSERT_IN_RANGE(0.0, cents_from(st.fundamental_hz, 220.0), 1.0);
	ASSERT_IN_RANGE(0.0, cents_from(st.lock_hz, 220.0), 1.0);
	tracker_free(&t);
	free(x);
	PASS();
}

TEST a_sub_oscillator_is_the_fundamental(void) {
	uint32_t n = 2 * FS;
	float *x = calloc(n, sizeof(float)), *sub = calloc(n, sizeof(float));
	test_signal_t main_osc = { SIGNAL_SAWTOOTH, 220.0, 0.0, 0.5, 0.0, 40, 1, 0.0 };
	test_signal_t sub_osc = { SIGNAL_SINE, 110.0, 0.0, 0.5 * 0.25, 0.3, 0, 1, 0.0 };   // 12 dB below the main
	test_signal_generate(&main_osc, x, n, FS);
	test_signal_generate(&sub_osc, sub, n, FS);
	for (uint32_t i = 0; i < n; i++) x[i] += sub[i];
	tracker_t *t = tracker_new(NULL, FS);
	ASSERT(t);
	double at = time_to_read(t, x, n, 110.0, 5.0);
	ASSERT(at > 0.0);
	ASSERT(at <= 0.5);
	tracker_state_t st = tracker_state(t);
	ASSERT_IN_RANGE(0.0, cents_from(st.fundamental_hz, 110.0), 1.0);
	tracker_free(&t);
	free(x);
	free(sub);
	PASS();
}

// A sine after a sine a twelfth below it: the lock left on the first note sits on three
// periods of the second, which the lone partial lifts and the waveform confirms, so the
// second note is read as itself.
TEST a_sine_after_a_twelfth_below_is_read_as_itself(void) {
	uint32_t n = 3 * FS;
	float *x = calloc(n, sizeof(float));
	test_signal_t low = { SIGNAL_SINE, 55.0, 0.0, 0.25, 0.0, 0, 1, 0.0 };
	test_signal_t high = { SIGNAL_SINE, 164.76, 0.0, 0.25, 0.0, 0, 1, 0.0 };
	test_signal_generate(&low, x, FS, FS);
	test_signal_generate(&high, x + FS, n - FS, FS);
	tracker_t *t = tracker_new(NULL, FS);
	ASSERT(t);
	tracker_feed(t, x, FS);
	ASSERT_IN_RANGE(0.0, cents_from(tracker_state(t).fundamental_hz, 55.0), 5.0);
	double at = time_to_read(t, x + FS, n - FS, 164.76, 5.0);
	ASSERT(at > 0.0);
	ASSERT(at <= 0.5);
	tracker_free(&t);
	free(x);
	PASS();
}

TEST silence_ends_the_lock_after_the_hold(void) {
	uint32_t n = 2 * FS;
	float *x = calloc(n, sizeof(float));
	test_signal_t s = { SIGNAL_SAWTOOTH, 440.0, 0.0, 0.5, 0.0, 30, 1, 0.0 };
	test_signal_generate(&s, x, FS, FS);   // one second of note, one of silence
	tracker_t *t = tracker_new(NULL, FS);
	ASSERT(t);
	double first_unlocked = -1.0;
	for (uint32_t at = 0; at + HOP <= n; at += HOP) {
		tracker_feed(t, x + at, HOP);
		tracker_state_t st = tracker_state(t);
		if (at + HOP == FS) ASSERT(st.locked && st.committed);
		if (at + HOP > FS && !st.locked && first_unlocked < 0.0) first_unlocked = (double)(at + HOP) / FS;
	}
	ASSERT(first_unlocked > 1.0);
	ASSERT(first_unlocked <= 1.0 + default_tracker_config.hold_s + 0.05);   // the hold, and the gate reading four hops
	ASSERT_FALSE(tracker_state(t).committed);
	ASSERT_EQ_FMT(0.0f, tracker_state(t).fundamental_hz, "%g");
	tracker_free(&t);
	free(x);
	PASS();
}

TEST noise_never_commits(void) {
	uint32_t n = 2 * FS;
	float *x = calloc(n, sizeof(float));
	test_signal_t s = { SIGNAL_NOISE, 0.0, 0.0, 0.3, 0.0, 0, 7, 0.0 };
	test_signal_generate(&s, x, n, FS);
	tracker_t *t = tracker_new(NULL, FS);
	ASSERT(t);
	for (uint32_t at = 0; at + HOP <= n; at += HOP) {
		tracker_feed(t, x + at, HOP);
		ASSERT_FALSE(tracker_state(t).committed);
	}
	tracker_free(&t);
	free(x);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(a_sawtooth_locks_and_reads_within_the_promise);
	RUN_TEST(a_sub_oscillator_is_the_fundamental);
	RUN_TEST(a_sine_after_a_twelfth_below_is_read_as_itself);
	RUN_TEST(silence_ends_the_lock_after_the_hold);
	RUN_TEST(noise_never_commits);
	GREATEST_MAIN_END();
}
