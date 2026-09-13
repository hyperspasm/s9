// The periodic analyzer end to end: audio in, frames rendered on SDL's
// software renderer, the fundamental and the harmonics view's readings out.
// Runs without a display. What the readings are made of is tested on the
// reader, and the tracking on the tracker.
#include "greatest.h"
#include "periodic.h"
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

static periodic_t *streaming(void) {
	periodic_t *p = periodic_new(NULL);
	periodic_prepare_to_stream(p, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 2,
		.buffer_frames = 256, .samples_per_display_frame = FRAME,
	});
	return p;
}

// one display frame: its audio, both channels alike, then the rendering
static void frame(periodic_t *p, const test_signal_t *s, unsigned *offset) {
	float *mono = malloc(sizeof(float) * (*offset + FRAME)), stereo[FRAME * 2];
	test_signal_generate(s, mono, *offset + FRAME, FS);
	for (unsigned i = 0; i < FRAME; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[*offset + i];
	free(mono);
	*offset += FRAME;
	periodic_enqueue_audio_data(p, stereo, sizeof(stereo));
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	periodic_render(p, renderer);
}

static unsigned lit_pixels(void) {
	unsigned lit = 0;
	const uint32_t *px = surface->pixels;
	for (int i = 0; i < WIDTH * HEIGHT; i++)
		if ((px[i] & 0xffffff00) != 0) lit++;
	return lit;
}

// lit pixels in one of the strips' bands across the top, the first being 0
static unsigned band_lit_pixels(int band) {
	unsigned lit = 0;
	const uint32_t *px = surface->pixels + band * WIDTH * STRIP_HEIGHT;
	for (int i = 0; i < WIDTH * STRIP_HEIGHT; i++)
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
	periodic_t *p = streaming();
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 120; i++) frame(p, &saw, &offset);
	ASSERT(lit_pixels() > 10000);

	uint32_t count;
	const harmonic_reading_t *reads = periodic_readings(p, &count);
	ASSERT_EQ(10u, count);
	for (uint32_t k = 1; k <= count; k++) {
		ASSERT(reads[k - 1].shown);
		ASSERT_IN_RANGE(-20.0 * log10(k), (double)(reads[k - 1].readout.db - reads[0].readout.db), 0.2);
		double expected = -90.0 + 90.0 * k + (k % 2 == 0 ? 180.0 : 0.0);
		ASSERT_IN_RANGE(0.0, angle_diff(reads[k - 1].phase_deg, expected), 1.0);
	}
	float thd;
	ASSERT_EQ(0, periodic_thd(p, &thd));
	ASSERT_IN_RANGE(0.7415, (double)thd, 0.01);
	periodic_free(&p);
	PASS();
}

// Once the tracker loses the note, the readings go with it.
TEST readings_go_with_the_lock(void) {
	periodic_t *p = streaming();
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	test_signal_t silence = { SIGNAL_SILENCE, 0, 0, 0, 0, 0, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 120; i++) frame(p, &saw, &offset);
	for (int i = 0; i < 120; i++) frame(p, &silence, &offset);
	ASSERT(!periodic_valid(p));

	uint32_t count;
	periodic_readings(p, &count);
	ASSERT_EQ(0u, count);
	float thd;
	ASSERT_EQ(-1, periodic_thd(p, &thd));
	periodic_free(&p);
	PASS();
}

// Reconfiguration while streaming keeps reading: the tracker's window and
// its function, and the harmonic count, which is clamped to what fits.
TEST reconfigures_while_streaming(void) {
	periodic_t *p = streaming();
	test_signal_t saw = { SIGNAL_SAWTOOTH, 110.0, 0, 0.5, 0.3, 10, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 60; i++) frame(p, &saw, &offset);

	periodic_config_t c = *periodic_config(p);
	c.tracker.window_s = 0.4f;
	c.tracker.window_function = WINDOW_FUNCTION_HANN;
	c.harmonics.reader.harmonics = 20;
	periodic_set_config(p, &c);
	for (int i = 0; i < 90; i++) frame(p, &saw, &offset);
	uint32_t count;
	const harmonic_reading_t *reads = periodic_readings(p, &count);
	ASSERT_EQ(20u, count);
	ASSERT(reads[0].shown);

	c.harmonics.reader.harmonics = 200;   // clamped to what fits, once applied at the frame
	periodic_set_config(p, &c);
	frame(p, &saw, &offset);
	periodic_readings(p, &count);
	ASSERT(count < 200);
	periodic_free(&p);
	PASS();
}

// Without a fundamental the spectrum is still drawn, on the configured
// axis, with no readings.
TEST noise_is_drawn_without_readings(void) {
	periodic_t *p = streaming();
	test_signal_t noise = { SIGNAL_NOISE, 0, 0, 0.5, 0, 0, 12345, 0 };
	unsigned offset = 0;
	uint32_t count;
	for (int i = 0; i < 60; i++) {
		frame(p, &noise, &offset);
		periodic_readings(p, &count);
		ASSERT_EQ(0u, count);
	}
	ASSERT(lit_pixels() > 10000);
	periodic_free(&p);
	PASS();
}

// The waveform view draws a sawtooth swept over one cycle of the tracked
// fundamental, and keeps drawing when the cycles change.
TEST waveform_view_draws_cycles(void) {
	periodic_t *p = streaming();
	periodic_config_t c = *periodic_config(p);
	c.view = PERIODIC_VIEW_WAVEFORM;
	periodic_set_config(p, &c);
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	unsigned offset = 0;
	for (int i = 0; i < 120; i++) frame(p, &saw, &offset);
	ASSERT(periodic_valid(p));
	ASSERT(lit_pixels() > 10000);

	c.waveform.cycles = 3;
	periodic_set_config(p, &c);
	for (int i = 0; i < 30; i++) frame(p, &saw, &offset);
	ASSERT(lit_pixels() > 10000);
	periodic_free(&p);
	PASS();
}

// The note is read from the fundamental at the reference and shown across
// the top, on the range and on the tuner's scale: a 220 Hz sawtooth is A3,
// in tune at A4 = 440 Hz, and flat by the cents between 440 and 442 Hz at
// a reference of 442. There is none before the tracker has locked.
TEST note_is_read_and_shown(void) {
	periodic_t *p = streaming();
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	unsigned offset = 0;
	note_t note;
	frame(p, &saw, &offset);
	ASSERT_FALSE(periodic_note(p, &note));
	for (int i = 0; i < 120; i++) frame(p, &saw, &offset);
	ASSERT(periodic_note(p, &note));
	ASSERT_EQ(57, note.number);
	ASSERT_IN_RANGE(0.0, (double)note.cents, 5.0);
	ASSERT(band_lit_pixels(0) > 200);
	ASSERT(band_lit_pixels(1) > 200);

	periodic_config_t c = *periodic_config(p);
	c.note.a4 = 442.0f;
	periodic_set_config(p, &c);
	frame(p, &saw, &offset);
	note_t at442;
	ASSERT(periodic_note(p, &at442));
	ASSERT_EQ(57, at442.number);
	ASSERT_IN_RANGE(-7.85, (double)(at442.cents - note.cents), 0.1);
	periodic_free(&p);
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
	RUN_TEST(waveform_view_draws_cycles);
	RUN_TEST(note_is_read_and_shown);
	GREATEST_MAIN_END();
}
