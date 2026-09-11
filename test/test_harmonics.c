// The harmonic analyzer end to end: audio in through the tracker's and the
// component's own paths, frames rendered on SDL's software renderer, the
// readings out. Runs without a display. What the readings are made of is
// tested on the reader, and the tracking on the tracker.
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

// the tracker and the component it feeds, streaming
typedef struct {
	pitch_tracker_t *tracker;
	harmonics_t *h;
} rig_t;

static rig_t streaming(void) {
	rig_t r;
	s9_audio_stream_config_t stream = {
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 2,
		.buffer_frames = 256, .samples_per_display_frame = FRAME,
	};
	r.tracker = pitch_tracker_new(NULL);
	r.h = harmonics_new(NULL, r.tracker);
	pitch_tracker_prepare_to_stream(r.tracker, &stream);
	harmonics_prepare_to_stream(r.h, &stream);
	return r;
}

static void rig_free(rig_t *r) {
	harmonics_free(&r->h);
	pitch_tracker_free(&r->tracker);
}

// one display frame: its audio to both, the tracker's update, then the rendering
static void frame(rig_t *r, const test_signal_t *s, unsigned *offset) {
	float *mono = malloc(sizeof(float) * (*offset + FRAME)), stereo[FRAME * 2];
	test_signal_generate(s, mono, *offset + FRAME, FS);
	for (unsigned i = 0; i < FRAME; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[*offset + i];
	free(mono);
	*offset += FRAME;
	pitch_tracker_enqueue_audio_data(r->tracker, stereo, sizeof(stereo));
	harmonics_enqueue_audio_data(r->h, stereo, sizeof(stereo));
	pitch_tracker_update(r->tracker);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	harmonics_render(r->h, renderer);
}

static unsigned lit_pixels(void) {
	unsigned lit = 0;
	const uint32_t *px = surface->pixels;
	for (int i = 0; i < WIDTH * HEIGHT; i++)
		if ((px[i] & 0xffffff00) != 0) lit++;
	return lit;
}

// the difference between two angles in degrees, in (-180, 180]
static double angle_diff(double a, double b) {
	double d = fmod(a - b, 360.0);
	if (d < -180.0) d += 360.0;
	if (d > 180.0) d -= 360.0;
	return d;
}

// A steady sawtooth is drawn with its readings: all ten harmonics, their
// levels against the fundamental's, their phases, and the distortion. The
// generator's harmonics are sines, the even ones negated, so harmonic k
// sits at -90 + 90k degrees, plus 180 when even.
TEST sawtooth_is_drawn_and_read(void) {
	rig_t r = streaming();
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 120; i++) frame(&r, &saw, &offset);
	ASSERT(lit_pixels() > 10000);

	uint32_t count;
	const harmonic_reading_t *reads = harmonics_readings(r.h, &count);
	ASSERT_EQ(10u, count);
	for (uint32_t k = 1; k <= count; k++) {
		ASSERT(reads[k - 1].shown);
		ASSERT_IN_RANGE(-20.0 * log10(k), (double)(reads[k - 1].readout.db - reads[0].readout.db), 0.2);
		double expected = -90.0 + 90.0 * k + (k % 2 == 0 ? 180.0 : 0.0);
		ASSERT_IN_RANGE(0.0, angle_diff(reads[k - 1].phase_deg, expected), 1.0);
	}
	float thd;
	ASSERT_EQ(0, harmonics_get_thd(r.h, &thd));
	ASSERT_IN_RANGE(0.7415, (double)thd, 0.01);
	rig_free(&r);
	PASS();
}

// Once the tracker loses the note, the readings go with it.
TEST readings_go_with_the_lock(void) {
	rig_t r = streaming();
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	test_signal_t silence = { SIGNAL_SILENCE, 0, 0, 0, 0, 0, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 120; i++) frame(&r, &saw, &offset);
	for (int i = 0; i < 120; i++) frame(&r, &silence, &offset);
	ASSERT(!pitch_tracker_valid(r.tracker));

	uint32_t count;
	harmonics_readings(r.h, &count);
	ASSERT_EQ(0u, count);
	float thd;
	ASSERT_EQ(-1, harmonics_get_thd(r.h, &thd));
	rig_free(&r);
	PASS();
}

// Reconfiguration while streaming keeps reading: resolution, window, and
// the harmonic count, which is clamped to what fits.
TEST reconfigures_while_streaming(void) {
	rig_t r = streaming();
	test_signal_t saw = { SIGNAL_SAWTOOTH, 110.0, 0, 0.5, 0.3, 10, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 60; i++) frame(&r, &saw, &offset);

	harmonics_config_t c = *harmonics_config(r.h);
	c.resolution_hz = 4.0f;
	c.window_function = WINDOW_FUNCTION_HANN;
	c.reader.harmonics = 20;
	harmonics_set_config(r.h, &c);
	for (int i = 0; i < 90; i++) frame(&r, &saw, &offset);
	uint32_t count;
	const harmonic_reading_t *reads = harmonics_readings(r.h, &count);
	ASSERT_EQ(20u, count);
	ASSERT(reads[0].shown);

	c.reader.harmonics = 200;
	harmonics_set_config(r.h, &c);
	ASSERT(harmonics_config(r.h)->reader.harmonics < 200);
	frame(&r, &saw, &offset);
	rig_free(&r);
	PASS();
}

// Without a fundamental the spectrum is still drawn, on the configured
// axis, with no readings.
TEST noise_is_drawn_without_readings(void) {
	rig_t r = streaming();
	test_signal_t noise = { SIGNAL_NOISE, 0, 0, 0.5, 0, 0, 12345, 0 };
	unsigned offset = 0;
	uint32_t count;
	for (int i = 0; i < 60; i++) {
		frame(&r, &noise, &offset);
		harmonics_readings(r.h, &count);
		ASSERT_EQ(0u, count);
	}
	ASSERT(lit_pixels() > 10000);
	rig_free(&r);
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
	RUN_TEST(sawtooth_is_drawn_and_read);
	RUN_TEST(readings_go_with_the_lock);
	RUN_TEST(reconfigures_while_streaming);
	RUN_TEST(noise_is_drawn_without_readings);
	GREATEST_MAIN_END();
}
