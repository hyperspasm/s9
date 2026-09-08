// The harmonic analyzer end to end: audio in through the component's own
// path, frames rendered on SDL's software renderer, the tracked fundamental
// out. Runs without a display.
#include "greatest.h"
#include "harmonics.h"
#include "test_signals.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FS 44100
#define FRAME 735          // samples per display frame at 60 Hz
#define WIDTH 1280
#define HEIGHT 720

static SDL_Surface *surface;
static SDL_Renderer *renderer;

static harmonics_t *streaming(const harmonics_config_t *config) {
	harmonics_t *h = harmonics_new(config);
	harmonics_prepare_to_stream(h, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 2,
		.buffer_frames = 256, .samples_per_display_frame = FRAME,
	});
	return h;
}

// stereo frames of a signal, both channels alike, continuing from sample offset
static void enqueue_frames(harmonics_t *h, const test_signal_t *s, unsigned frames, unsigned *offset) {
	unsigned n = frames * FRAME;
	float *mono = malloc(sizeof(float) * (*offset + n)), *stereo = malloc(sizeof(float) * n * 2);
	test_signal_generate(s, mono, *offset + n, FS);
	for (unsigned i = 0; i < n; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[*offset + i];
	harmonics_enqueue_audio_data(h, stereo, sizeof(float) * n * 2);
	*offset += n;
	free(mono);
	free(stereo);
}

// one display frame: its audio, then its rendering
static void frame(harmonics_t *h, const test_signal_t *s, unsigned *offset) {
	enqueue_frames(h, s, 1, offset);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	harmonics_render(h, renderer);
}

static double cents_from(double f, double reference) {
	return 1200.0 * log2(f / reference);
}

static unsigned lit_pixels(void) {
	unsigned lit = 0;
	const uint32_t *px = surface->pixels;
	for (int i = 0; i < WIDTH * HEIGHT; i++)
		if ((px[i] & 0xffffff00) != 0) lit++;
	return lit;
}

// A steady sawtooth is tracked within the dead band, and drawn.
TEST tracks_a_sawtooth(void) {
	harmonics_t *h = streaming(NULL);
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 120; i++) frame(h, &saw, &offset);
	ASSERT(harmonics_get_pitch_valid(h));
	ASSERT_IN_RANGE(0.0, cents_from(harmonics_get_pitch_frequency(h), 220.0), 5.0);
	ASSERT(lit_pixels() > 10000);
	harmonics_free(&h);
	PASS();
}

// After the signal stops, the fundamental is held for hold_frames frames,
// then reported invalid while its value stays.
TEST holds_then_drops_on_silence(void) {
	harmonics_t *h = streaming(NULL);
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	test_signal_t silence = { SIGNAL_SILENCE, 0, 0, 0, 0, 0, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 120; i++) frame(h, &saw, &offset);
	ASSERT(harmonics_get_pitch_valid(h));
	float held = harmonics_get_pitch_frequency(h);

	// the analysis window is long: the tone lingers in it for a while
	uint32_t hold = harmonics_config(h)->hold_frames;
	int dropped_at = -1;
	for (int i = 0; i < 120 && dropped_at < 0; i++) {
		frame(h, &silence, &offset);
		if (!harmonics_get_pitch_valid(h)) dropped_at = i;
	}
	ASSERT(dropped_at >= (int)hold);
	ASSERT_EQ_FMT(held, harmonics_get_pitch_frequency(h), "%g");

	// no readings without the lock
	uint32_t count;
	harmonics_readings(h, &count);
	ASSERT_EQ(0u, count);
	float thd;
	ASSERT_EQ(-1, harmonics_get_thd(h, &thd));
	harmonics_free(&h);
	PASS();
}

// Reconfiguration while streaming keeps tracking: resolution, window,
// harmonic count, and the channel analyzed.
TEST reconfigures_while_streaming(void) {
	harmonics_t *h = streaming(NULL);
	test_signal_t saw = { SIGNAL_SAWTOOTH, 110.0, 0, 0.5, 0.3, 10, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 60; i++) frame(h, &saw, &offset);

	harmonics_config_t c = *harmonics_config(h);
	c.resolution_hz = 4.0f;
	c.window_function = WINDOW_FUNCTION_HANN;
	c.harmonics = 20;
	c.channel = 1;
	harmonics_set_config(h, &c);
	for (int i = 0; i < 90; i++) frame(h, &saw, &offset);
	ASSERT(harmonics_get_pitch_valid(h));
	ASSERT_IN_RANGE(0.0, cents_from(harmonics_get_pitch_frequency(h), 110.0), 5.0);

	c.harmonics = 200;   // clamped to what fits
	harmonics_set_config(h, &c);
	ASSERT(harmonics_config(h)->harmonics < 200);
	frame(h, &saw, &offset);
	harmonics_free(&h);
	PASS();
}

// Noise never becomes a pitch; the spectrum is still drawn, on the
// configured axis, with no readings.
TEST noise_has_no_pitch(void) {
	harmonics_t *h = streaming(NULL);
	test_signal_t noise = { SIGNAL_NOISE, 0, 0, 0.5, 0, 0, 12345, 0 };
	unsigned offset = 0;
	uint32_t count;
	for (int i = 0; i < 60; i++) {
		frame(h, &noise, &offset);
		ASSERT(!harmonics_get_pitch_valid(h));
		harmonics_readings(h, &count);
		ASSERT_EQ(0u, count);
	}
	ASSERT_EQ_FMT(0.0f, harmonics_get_pitch_frequency(h), "%g");
	ASSERT(lit_pixels() > 10000);
	harmonics_free(&h);
	PASS();
}

// A sine in noise reads one harmonic, the fundamental at its level once
// the average has settled, and no distortion.
TEST sine_in_noise_reads_one_harmonic(void) {
	harmonics_t *h = streaming(NULL);
	test_signal_t sine = { SIGNAL_SINE, 1000.0, 0, 0.5, 0.3, 0, 7, 0.02 };
	unsigned offset = 0;
	for (int i = 0; i < 300; i++) frame(h, &sine, &offset);
	ASSERT(harmonics_get_pitch_valid(h));

	uint32_t count;
	const harmonics_reading_t *reads = harmonics_readings(h, &count);
	ASSERT_EQ(10u, count);
	ASSERT(reads[0].shown);
	ASSERT_IN_RANGE(-6.02, (double)reads[0].readout.db, 0.1);
	for (uint32_t k = 2; k <= count; k++) ASSERT_FALSE(reads[k - 1].shown);
	float thd;
	ASSERT_EQ(0, harmonics_get_thd(h, &thd));
	ASSERT_EQ_FMT(0.0f, thd, "%g");
	harmonics_free(&h);
	PASS();
}

// A sawtooth of ten harmonics reads all ten and the distortion they add up to.
TEST sawtooth_reads_its_distortion(void) {
	harmonics_t *h = streaming(NULL);
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 120; i++) frame(h, &saw, &offset);
	ASSERT(harmonics_get_pitch_valid(h));

	uint32_t count;
	const harmonics_reading_t *reads = harmonics_readings(h, &count);
	ASSERT_EQ(10u, count);
	for (uint32_t k = 1; k <= count; k++) {
		ASSERT(reads[k - 1].shown);
		ASSERT_IN_RANGE(-20.0 * log10(k), (double)(reads[k - 1].readout.db - reads[0].readout.db), 0.2);
	}
	float thd;
	ASSERT_EQ(0, harmonics_get_thd(h, &thd));
	ASSERT_IN_RANGE(0.7415, (double)thd, 0.01);
	harmonics_free(&h);
	PASS();
}

// one display frame of a sine with a weak third harmonic in noise
static void weak_frame(harmonics_t *h, double f0, double weak_amp, unsigned *offset) {
	test_signal_t sine = { SIGNAL_SINE, f0, 0, 0.5, 0.3, 0, 9, 0.01 };
	test_signal_t weak = { SIGNAL_SINE, 3 * f0, 0, weak_amp, 1.1, 0, 0, 0 };
	float a[FRAME], b[FRAME], stereo[FRAME * 2];
	float *whole = calloc(*offset + FRAME, sizeof(float));
	test_signal_generate(&sine, whole, *offset + FRAME, FS);
	memcpy(a, whole + *offset, sizeof(a));
	test_signal_generate(&weak, whole, *offset + FRAME, FS);
	memcpy(b, whole + *offset, sizeof(b));
	free(whole);
	for (unsigned i = 0; i < FRAME; i++) stereo[2 * i] = stereo[2 * i + 1] = a[i] + b[i];
	harmonics_enqueue_audio_data(h, stereo, sizeof(stereo));
	*offset += FRAME;
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	harmonics_render(h, renderer);
}

// A harmonic a little above the floor is shown steadily once the average
// has settled, and the harmonics that are not there never are.
TEST weak_harmonic_is_shown_steadily(void) {
	harmonics_t *h = streaming(NULL);
	unsigned offset = 0;
	int changes = 0, empty_shown = 0;
	bool last = false;
	for (int i = 0; i < 240; i++) {
		weak_frame(h, 220.0, 3.7e-4, &offset);   // about 15 dB over the floor's median
		uint32_t count;
		const harmonics_reading_t *reads = harmonics_readings(h, &count);
		if (i < 120) continue;                     // the average settles
		if (count >= 3 && reads[2].shown != last) { changes++; last = reads[2].shown; }
		for (uint32_t k = 2; k <= count; k++)
			if (k != 3 && reads[k - 1].shown) empty_shown++;
	}
	ASSERT(harmonics_get_pitch_valid(h));
	ASSERT(last);
	ASSERT(changes <= 1);
	ASSERT_EQ(0, empty_shown);
	harmonics_free(&h);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	setenv("SDL_VIDEODRIVER", "dummy", 1);
	if (SDL_Init(SDL_INIT_VIDEO) != 0 || TTF_Init() != 0) {
		fprintf(stderr, "SDL unavailable: %s\n", SDL_GetError());
		return 1;
	}
	surface = SDL_CreateRGBSurfaceWithFormat(0, WIDTH, HEIGHT, 32, SDL_PIXELFORMAT_RGBA8888);
	renderer = SDL_CreateSoftwareRenderer(surface);
	if (!surface || !renderer) {
		fprintf(stderr, "software renderer unavailable: %s\n", SDL_GetError());
		return 1;
	}
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	GREATEST_MAIN_BEGIN();
	RUN_TEST(tracks_a_sawtooth);
	RUN_TEST(holds_then_drops_on_silence);
	RUN_TEST(reconfigures_while_streaming);
	RUN_TEST(noise_has_no_pitch);
	RUN_TEST(sine_in_noise_reads_one_harmonic);
	RUN_TEST(sawtooth_reads_its_distortion);
	RUN_TEST(weak_harmonic_is_shown_steadily);
	GREATEST_MAIN_END();
}
