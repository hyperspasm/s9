// The spectrogram end to end: audio in, frames rendered on SDL's software
// renderer, rows out. Runs without a display. Rows follow the audio, one
// per hop once the transform has filled, however the hops arrive, and the
// image starts over when the transform changes. What a row holds is tested
// on the readout.
#include "greatest.h"
#include "spectrogram.h"
#include "test_signals.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdlib.h>
#include <string.h>

#define FS 48000
#define HOP_MS 10.0f
#define HOP 480            // samples in HOP_MS at FS
#define FRAME 800          // samples per display frame at 60 Hz
#define BUFFER 256         // frames per audio callback
#define WIDTH 1280
#define HEIGHT 720

static SDL_Surface *surface;
static SDL_Renderer *renderer;

static spectrogram_t *streaming(float window_s) {
	spectrogram_config_t c = default_spectrogram_config;
	c.hop_ms = HOP_MS;
	c.window_s = window_s;
	spectrogram_t *sg = spectrogram_new(&c);
	spectrogram_prepare_to_stream(sg, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 2,
		.buffer_frames = BUFFER, .samples_per_display_frame = FRAME,
	});
	return sg;
}

// hops of a tone, both channels alike, queued as the audio thread would
static void feed(spectrogram_t *sg, unsigned hops, unsigned *offset) {
	test_signal_t s = { SIGNAL_SINE, 1000.0, 0, 0.5, 0, 0, 0, 0 };
	unsigned n = hops * HOP;
	float *mono = malloc(sizeof(float) * (*offset + n)), *stereo = malloc(sizeof(float) * n * 2);
	test_signal_generate(&s, mono, *offset + n, FS);
	for (unsigned i = 0; i < n; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[*offset + i];
	*offset += n;
	spectrogram_enqueue_audio_data(sg, stereo, sizeof(float) * n * 2);
	free(stereo);
	free(mono);
}

static void render(spectrogram_t *sg) {
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	spectrogram_render(sg, renderer);
}

static unsigned lit_pixels(void) {
	unsigned lit = 0;
	const uint32_t *px = surface->pixels;
	for (int i = 0; i < WIDTH * HEIGHT; i++)
		if ((px[i] & 0xffffff00) != 0) lit++;
	return lit;
}

// the hops it takes to fill a transform of n samples
static unsigned hops_to_fill(unsigned n) { return (n + HOP - 1) / HOP; }

// No row until the transform has filled; then one per hop, whether the
// hops come one per frame or three at once, and a frame without audio
// writes none. Rows light the plot, even fed faster than real time, when
// the audio clock re-seats rather than following.
TEST rows_follow_the_hops(void) {
	enum { N = 32768, MORE = 30 };   // the longest power of two within 0.75 s; rows past the first
	spectrogram_t *sg = streaming(0.75f);
	unsigned offset = 0;
	render(sg);
	unsigned axes_only = lit_pixels();

	for (unsigned h = 1; h < hops_to_fill(N); h++) {
		feed(sg, 1, &offset);
		render(sg);
		ASSERT_EQ_FMT(0u, spectrogram_rows(sg), "%u");
	}
	feed(sg, 1, &offset);
	render(sg);
	ASSERT_EQ_FMT(1u, spectrogram_rows(sg), "%u");
	feed(sg, 1, &offset);
	render(sg);
	ASSERT_EQ_FMT(2u, spectrogram_rows(sg), "%u");

	feed(sg, 3, &offset);
	render(sg);
	ASSERT_EQ_FMT(5u, spectrogram_rows(sg), "%u");
	render(sg);
	ASSERT_EQ_FMT(5u, spectrogram_rows(sg), "%u");

	for (unsigned h = 0; h < MORE; h++) {
		feed(sg, 1, &offset);
		render(sg);
	}
	ASSERT_EQ_FMT(5u + MORE, spectrogram_rows(sg), "%u");
	ASSERT(lit_pixels() > axes_only);

	spectrogram_free(&sg);
	PASS();
}

// A new window length starts the image over, and rows resume once the new
// transform has filled.
TEST new_transform_starts_over(void) {
	enum { N = 32768, SHORTER = 8192 };
	spectrogram_t *sg = streaming(0.75f);
	unsigned offset = 0;
	for (unsigned h = 0; h < hops_to_fill(N) + 3; h++) {
		feed(sg, 1, &offset);
		render(sg);
	}
	ASSERT_EQ_FMT(4u, spectrogram_rows(sg), "%u");

	spectrogram_config_t c = *spectrogram_config(sg);
	c.window_s = (float)SHORTER / FS + 0.01f;
	spectrogram_set_config(sg, &c);
	for (unsigned h = 1; h < hops_to_fill(SHORTER); h++) {
		feed(sg, 1, &offset);
		render(sg);
		ASSERT_EQ_FMT(0u, spectrogram_rows(sg), "%u");
	}
	feed(sg, 1, &offset);
	render(sg);
	ASSERT_EQ_FMT(1u, spectrogram_rows(sg), "%u");

	spectrogram_free(&sg);
	PASS();
}

// A configuration that makes no sense falls back to the default.
TEST config_is_kept_sane(void) {
	spectrogram_t *sg = spectrogram_new(NULL);
	spectrogram_config_t c = default_spectrogram_config;
	c.hop_ms = 0.0f;
	c.min_db = 0.0f;
	c.max_db = -10.0f;
	c.resolution_semitones = 0.0f;
	c.f_min = 100.0f;
	c.f_max = 50.0f;
	spectrogram_set_config(sg, &c);
	const spectrogram_config_t *got = spectrogram_config(sg);
	ASSERT_EQ_FMT(default_spectrogram_config.hop_ms, got->hop_ms, "%g");
	ASSERT_EQ_FMT(default_spectrogram_config.min_db, got->min_db, "%g");
	ASSERT_EQ_FMT(default_spectrogram_config.max_db, got->max_db, "%g");
	ASSERT_EQ_FMT(default_spectrogram_config.resolution_semitones, got->resolution_semitones, "%g");
	ASSERT_EQ_FMT(default_spectrogram_config.f_min, got->f_min, "%g");
	ASSERT_EQ_FMT(default_spectrogram_config.f_max, got->f_max, "%g");
	spectrogram_free(&sg);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
	if (SDL_Init(SDL_INIT_VIDEO) != 0 || TTF_Init() != 0) {
		fprintf(stderr, "SDL: %s\n", SDL_GetError());
		return 1;
	}
	surface = SDL_CreateRGBSurfaceWithFormat(0, WIDTH, HEIGHT, 32, SDL_PIXELFORMAT_RGBA8888);
	renderer = SDL_CreateSoftwareRenderer(surface);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	GREATEST_MAIN_BEGIN();
	RUN_TEST(rows_follow_the_hops);
	RUN_TEST(new_transform_starts_over);
	RUN_TEST(config_is_kept_sane);
	GREATEST_MAIN_END();
}
