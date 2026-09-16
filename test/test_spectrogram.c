// The spectrogram end to end: audio in, frames rendered on SDL's software
// renderer, rows out. Runs without a display. Rows follow the audio, one
// per hop once the frame has filled, however the hops arrive, and the
// image starts over when the frame changes. What a row holds is tested on
// the readout.
#include "greatest.h"
#include "spectrogram.h"
#include "test_signals.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FS 48000
#define HOP_MS 10
#define HOP 480            // samples in HOP_MS at FS
#define FRAME 800          // samples per display frame at 60 Hz
#define BUFFER 256         // frames per audio callback
#define WIDTH 1280
#define HEIGHT 720

static SDL_Surface *surface;
static SDL_Renderer *renderer;

// streaming with the range starting at f_min, which sets the frame's length
static spectrogram_t *streaming(float f_min) {
	spectrogram_config_t c = default_spectrogram_config;
	c.hop_ms = HOP_MS;
	c.f_min = f_min;
	spectrogram_t *sg = spectrogram_new(&c);
	spectrogram_prepare_to_stream(sg, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 2,
		.buffer_frames = BUFFER, .samples_per_display_frame = FRAME,
	});
	return sg;
}

// hops of a tone at an amplitude, both channels alike, queued as the audio thread would
static void feed_tone(spectrogram_t *sg, unsigned hops, unsigned *offset, double amplitude) {
	test_signal_t s = { SIGNAL_SINE, 1000.0, 0, amplitude, 0, 0, 0, 0 };
	unsigned n = hops * HOP;
	float *mono = malloc(sizeof(float) * (*offset + n)), *stereo = malloc(sizeof(float) * n * 2);
	test_signal_generate(&s, mono, *offset + n, FS);
	for (unsigned i = 0; i < n; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[*offset + i];
	*offset += n;
	spectrogram_enqueue_audio_data(sg, stereo, sizeof(float) * n * 2);
	free(stereo);
	free(mono);
}

// hops of the tone at half scale, -6 dB
static void feed(spectrogram_t *sg, unsigned hops, unsigned *offset) {
	feed_tone(sg, hops, offset, 0.5);
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

// lit pixels whose color is not a grey
static unsigned colored_pixels(void) {
	unsigned colored = 0;
	const uint32_t *px = surface->pixels;
	for (int i = 0; i < WIDTH * HEIGHT; i++) {
		Uint8 r, g, b;
		SDL_GetRGB(px[i], surface->format, &r, &g, &b);
		if ((r || g || b) && (r != g || g != b)) colored++;
	}
	return colored;
}

// lit pixels in the rows from y0 up to y1
static unsigned lit_pixels_in_rows(int y0, int y1) {
	unsigned lit = 0;
	const uint32_t *px = surface->pixels;
	for (int i = y0 * WIDTH; i < y1 * WIDTH; i++)
		if ((px[i] & 0xffffff00) != 0) lit++;
	return lit;
}

// the separate runs of lit pixels down the column at x
static unsigned lit_runs_down(int x) {
	unsigned runs = 0;
	int in = 0;
	const uint32_t *px = surface->pixels;
	for (int y = 0; y < HEIGHT; y++) {
		int lit = (px[y * WIDTH + x] & 0xffffff00) != 0;
		if (lit && !in) runs++;
		in = lit;
	}
	return runs;
}

// the hops it takes to fill a frame of n samples
static unsigned hops_to_fill(unsigned n) { return (n + HOP - 1) / HOP; }

// No row until the frame has filled; then one per hop, whether the hops
// come one per display frame or three at once, and a display frame
// without audio writes none. Rows light the plot, even fed faster than
// real time, when the audio clock re-seats rather than following.
TEST rows_follow_the_hops(void) {
	enum { MORE = 30 };   // rows past the first
	spectrogram_t *sg = streaming(100.0f);
	unsigned N = spectrogram_frame(sg);
	ASSERT_EQ_FMT(21600u, N, "%u");   // the shortest length of factors 2, 3 and 5 holding a semitone's window at 100 Hz
	ASSERT_IN_RANGE((float)N / FS, spectrogram_time_resolution(sg), 1e-6f);
	// the shortest window, two hops, is a semitone's window at this frequency
	ASSERT_IN_RANGE(window_function_bandwidth(WINDOW_FUNCTION_BLACKMAN_HARRIS) * FS / (2.0 * HOP * (pow(2.0, 1.0 / 12.0) - 1.0)),
	                (double)spectrogram_floor_frequency(sg), 0.01);
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

// A range starting higher needs a shorter frame; the new frame starts the
// image over, and rows resume once it has filled.
TEST new_frame_starts_over(void) {
	spectrogram_t *sg = streaming(100.0f);
	unsigned N = spectrogram_frame(sg);
	unsigned offset = 0;
	for (unsigned h = 0; h < hops_to_fill(N) + 3; h++) {
		feed(sg, 1, &offset);
		render(sg);
	}
	ASSERT_EQ_FMT(4u, spectrogram_rows(sg), "%u");

	spectrogram_config_t c = *spectrogram_config(sg);
	c.f_min = 200.0f;
	spectrogram_set_config(sg, &c);
	unsigned SHORTER = spectrogram_frame(sg);
	ASSERT_EQ_FMT(10800u, SHORTER, "%u");
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

// A new color scale colors the whole history again, not only the rows
// that follow: a tone drawn in inferno is in color, and in grey nothing
// on the screen is.
TEST scale_change_recolors_the_history(void) {
	enum { ROWS = 30 };
	spectrogram_t *sg = streaming(100.0f);
	unsigned N = spectrogram_frame(sg);
	unsigned offset = 0;
	for (unsigned h = 0; h < hops_to_fill(N) + ROWS; h++) {
		feed(sg, 1, &offset);
		render(sg);
	}
	ASSERT_EQ_FMT((unsigned)ROWS + 1, spectrogram_rows(sg), "%u");
	ASSERT(colored_pixels() > 0);

	spectrogram_config_t c = *spectrogram_config(sg);
	c.palette = PALETTE_GREY;
	spectrogram_set_config(sg, &c);
	render(sg);
	ASSERT_EQ_FMT((unsigned)ROWS + 1, spectrogram_rows(sg), "%u");
	ASSERT_EQ_FMT(0u, colored_pixels(), "%u");
	ASSERT(lit_pixels() > 0);

	spectrogram_free(&sg);
	PASS();
}

// The grid is off unless asked for: nothing crosses an empty plot but its
// frame, so down the middle the frame's top and bottom edges are the only
// marks. Turned on, the grid has a line at every labelled second. The
// labels below the plot are there either way, unchanged.
TEST grid_is_off_until_turned_on(void) {
	enum { LABEL_ROWS = 24 };   // the bottom rows, holding the frequency labels and nothing else
	spectrogram_t *sg = streaming(100.0f);
	render(sg);
	unsigned without_grid = lit_pixels();
	unsigned labels = lit_pixels_in_rows(HEIGHT - LABEL_ROWS, HEIGHT);
	ASSERT(labels > 0);
	ASSERT_EQ_FMT(2u, lit_runs_down(WIDTH / 2), "%u");

	spectrogram_config_t c = *spectrogram_config(sg);
	c.grid = 1;
	spectrogram_set_config(sg, &c);
	render(sg);
	ASSERT(lit_runs_down(WIDTH / 2) > 2);
	ASSERT_EQ_FMT(labels, lit_pixels_in_rows(HEIGHT - LABEL_ROWS, HEIGHT), "%u");
	ASSERT(lit_pixels() > without_grid);

	spectrogram_free(&sg);
	PASS();
}

// With auto_range on, the top follows the material at the configured rates:
// a tone at -6 dB brings max_db down from 0 at the fall rate until it sits
// on the tone; a louder tone lifts it at the rise rate once enough of the
// image shows it; with following off again, the top stays where it was.
TEST auto_range_follows_the_material(void) {
	spectrogram_config_t c = default_spectrogram_config;
	c.hop_ms = HOP_MS;
	c.f_min = 100.0f;
	c.auto_range = 1;
	spectrogram_t *sg = spectrogram_new(&c);
	spectrogram_prepare_to_stream(sg, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 2,
		.buffer_frames = BUFFER, .samples_per_display_frame = FRAME,
	});
	unsigned offset = 0;
	for (unsigned h = 0; h < hops_to_fill(spectrogram_frame(sg)); h += 2) { feed(sg, 2, &offset); render(sg); }
	ASSERT_IN_RANGE(0.0f, spectrogram_config(sg)->max_db, 0.1f);

	for (unsigned h = 0; h < 200; h += 2) { feed(sg, 2, &offset); render(sg); }   // 2 s at 2 dB/s
	ASSERT_IN_RANGE(-4.0f, spectrogram_config(sg)->max_db, 0.1f);
	for (unsigned h = 0; h < 200; h += 2) { feed(sg, 2, &offset); render(sg); }
	ASSERT_IN_RANGE(-6.0f, spectrogram_config(sg)->max_db, 0.1f);

	for (unsigned h = 0; h < 500; h += 2) { feed_tone(sg, 2, &offset, 0.7); render(sg); }   // -3.1 dB
	ASSERT_IN_RANGE(-3.0f, spectrogram_config(sg)->max_db, 0.1f);

	c = *spectrogram_config(sg);
	c.auto_range = 0;
	spectrogram_set_config(sg, &c);
	float held = spectrogram_config(sg)->max_db;
	for (unsigned h = 0; h < 200; h += 2) { feed(sg, 2, &offset); render(sg); }
	ASSERT_EQ_FMT(held, spectrogram_config(sg)->max_db, "%g");

	spectrogram_free(&sg);
	PASS();
}

// A configuration that makes no sense falls back to the default.
TEST config_is_kept_sane(void) {
	spectrogram_t *sg = spectrogram_new(NULL);
	spectrogram_config_t c = default_spectrogram_config;
	c.hop_ms = 0;
	c.range_db = 0.0f;
	c.palette = (palette_t)PALETTE_COUNT;
	c.resolution_semitones = 0.0f;
	c.f_min = 100.0f;
	c.f_max = 50.0f;
	spectrogram_set_config(sg, &c);
	const spectrogram_config_t *got = spectrogram_config(sg);
	ASSERT_EQ_FMT(default_spectrogram_config.hop_ms, got->hop_ms, "%u");
	ASSERT_EQ_FMT(default_spectrogram_config.range_db, got->range_db, "%g");
	ASSERT_EQ(default_spectrogram_config.palette, got->palette);
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
	RUN_TEST(new_frame_starts_over);
	RUN_TEST(scale_change_recolors_the_history);
	RUN_TEST(grid_is_off_until_turned_on);
	RUN_TEST(auto_range_follows_the_material);
	RUN_TEST(config_is_kept_sane);
	GREATEST_MAIN_END();
}
