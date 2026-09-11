// The harmonic analyzer end to end: audio in through the component's own
// path, frames rendered on SDL's software renderer, the tracked fundamental
// and the readings out. Runs without a display. What the readings are made
// of is tested on the reader itself.
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

// After the signal stops, the fundamental is held for the hold time, then
// reported invalid while its value stays.
TEST holds_then_drops_on_silence(void) {
	harmonics_t *h = streaming(NULL);
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	test_signal_t silence = { SIGNAL_SILENCE, 0, 0, 0, 0, 0, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 120; i++) frame(h, &saw, &offset);
	ASSERT(harmonics_get_pitch_valid(h));
	float held = harmonics_get_pitch_frequency(h);

	// the analysis window is long: the tone lingers in it for a while
	int hold = (int)(harmonics_config(h)->tracker.hold_s * FS / FRAME);   // in display frames
	int dropped_at = -1;
	for (int i = 0; i < 120 && dropped_at < 0; i++) {
		frame(h, &silence, &offset);
		if (!harmonics_get_pitch_valid(h)) dropped_at = i;
	}
	ASSERT(dropped_at >= hold);
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
	c.reader.harmonics = 20;
	c.channel = 1;
	harmonics_set_config(h, &c);
	for (int i = 0; i < 90; i++) frame(h, &saw, &offset);
	ASSERT(harmonics_get_pitch_valid(h));
	ASSERT_IN_RANGE(0.0, cents_from(harmonics_get_pitch_frequency(h), 110.0), 5.0);

	c.reader.harmonics = 200;   // clamped to what fits
	harmonics_set_config(h, &c);
	ASSERT(harmonics_config(h)->reader.harmonics < 200);
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

// the difference between two angles in degrees, in (-180, 180]
static double angle_diff(double a, double b) {
	double d = fmod(a - b, 360.0);
	if (d < -180.0) d += 360.0;
	if (d > 180.0) d -= 360.0;
	return d;
}

// Through the component, a sawtooth of ten harmonics reads all ten, their
// phases, and the distortion they add up to. The generator's harmonics are
// sines, the even ones negated, so harmonic k sits at -90 + 90k degrees,
// plus 180 when even.
TEST sawtooth_reads_its_distortion(void) {
	harmonics_t *h = streaming(NULL);
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 120; i++) frame(h, &saw, &offset);
	ASSERT(harmonics_get_pitch_valid(h));

	uint32_t count;
	const harmonic_reading_t *reads = harmonics_readings(h, &count);
	ASSERT_EQ(10u, count);
	for (uint32_t k = 1; k <= count; k++) {
		ASSERT(reads[k - 1].shown);
		ASSERT_IN_RANGE(-20.0 * log10(k), (double)(reads[k - 1].readout.db - reads[0].readout.db), 0.2);
		double expected = -90.0 + 90.0 * k + (k % 2 == 0 ? 180.0 : 0.0);
		ASSERT_IN_RANGE(0.0, angle_diff(reads[k - 1].phase_deg, expected), 1.0);
	}
	float thd;
	ASSERT_EQ(0, harmonics_get_thd(h, &thd));
	ASSERT_IN_RANGE(0.7415, (double)thd, 0.01);
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
	RUN_TEST(sawtooth_reads_its_distortion);
	GREATEST_MAIN_END();
}
