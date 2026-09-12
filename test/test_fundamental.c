// The fundamental tracker as a component: audio in through its own path, the
// fundamental out, frame by frame.
#include "greatest.h"
#include "fundamental.h"
#include "test_signals.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FS 44100
#define FRAME 735   // samples per display frame at 60 Hz

static fundamental_t *streaming(const fundamental_config_t *config) {
	fundamental_t *t = fundamental_new(config);
	fundamental_prepare_to_stream(t, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 2,
		.buffer_frames = 256, .samples_per_display_frame = FRAME,
	});
	return t;
}

// one display frame of a signal, both channels alike, continuing from sample offset
static void frame(fundamental_t *t, const test_signal_t *s, unsigned *offset) {
	float *mono = malloc(sizeof(float) * (*offset + FRAME)), stereo[FRAME * 2];
	test_signal_generate(s, mono, *offset + FRAME, FS);
	for (unsigned i = 0; i < FRAME; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[*offset + i];
	free(mono);
	*offset += FRAME;
	fundamental_enqueue_audio_data(t, stereo, sizeof(stereo));
	fundamental_update(t);
}

static double cents_from(double f, double reference) {
	return 1200.0 * log2(f / reference);
}

// A steady sawtooth is tracked within the dead band.
TEST tracks_a_sawtooth(void) {
	fundamental_t *t = streaming(NULL);
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 120; i++) frame(t, &saw, &offset);
	ASSERT(fundamental_valid(t));
	ASSERT_IN_RANGE(0.0, cents_from(fundamental_frequency(t), 220.0), 5.0);
	fundamental_free(&t);
	PASS();
}

// After the signal stops, the fundamental is held for the hold time, then
// reported invalid while its value stays.
TEST holds_then_drops_on_silence(void) {
	fundamental_t *t = streaming(NULL);
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	test_signal_t silence = { SIGNAL_SILENCE, 0, 0, 0, 0, 0, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 120; i++) frame(t, &saw, &offset);
	ASSERT(fundamental_valid(t));
	float held = fundamental_frequency(t);

	// the analysis window is long: the tone lingers in it for a while
	int hold = (int)(fundamental_config(t)->conditioner.hold_s * FS / FRAME);   // in display frames
	int dropped_at = -1;
	for (int i = 0; i < 120 && dropped_at < 0; i++) {
		frame(t, &silence, &offset);
		if (!fundamental_valid(t)) dropped_at = i;
	}
	ASSERT(dropped_at >= hold);
	ASSERT_EQ_FMT(held, fundamental_frequency(t), "%g");
	fundamental_free(&t);
	PASS();
}

// Reconfiguration while streaming keeps tracking: the window's length and
// function, and the channel analyzed.
TEST reconfigures_while_streaming(void) {
	fundamental_t *t = streaming(NULL);
	test_signal_t saw = { SIGNAL_SAWTOOTH, 110.0, 0, 0.5, 0.3, 10, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 60; i++) frame(t, &saw, &offset);

	fundamental_config_t c = *fundamental_config(t);
	c.window_s = 0.1f;
	c.window_function = WINDOW_FUNCTION_HANN;
	c.channel = 1;
	fundamental_set_config(t, &c);
	for (int i = 0; i < 90; i++) frame(t, &saw, &offset);
	ASSERT(fundamental_valid(t));
	ASSERT_IN_RANGE(0.0, cents_from(fundamental_frequency(t), 110.0), 5.0);
	fundamental_free(&t);
	PASS();
}

// Noise never yields a fundamental.
TEST noise_has_no_fundamental(void) {
	fundamental_t *t = streaming(NULL);
	test_signal_t noise = { SIGNAL_NOISE, 0, 0, 0.5, 0, 0, 12345, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 60; i++) {
		frame(t, &noise, &offset);
		ASSERT(!fundamental_valid(t));
	}
	ASSERT_EQ_FMT(0.0f, fundamental_frequency(t), "%g");
	fundamental_free(&t);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(tracks_a_sawtooth);
	RUN_TEST(holds_then_drops_on_silence);
	RUN_TEST(reconfigures_while_streaming);
	RUN_TEST(noise_has_no_fundamental);
	GREATEST_MAIN_END();
}
