// The spectrum end to end: audio in, frames rendered on SDL's software
// renderer. Runs without a display. The frame follows the resolution at
// the bottom of the range, a configuration that makes no sense falls back
// to the default, and a tone peaks at its frequency. What the bins hold is
// tested on the analyzer.
#include "greatest.h"
#include "spectrum.h"
#include "fft.h"
#include "test_signals.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FS 48000
#define FRAME 800          // samples per display frame at 60 Hz
#define WIDTH 1280
#define HEIGHT 720

static SDL_Surface *surface;
static SDL_Renderer *renderer;

static spectrum_t *streaming(void) {
	spectrum_t *sp = spectrum_new(NULL);
	spectrum_prepare_to_stream(sp, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 2,
		.buffer_frames = 256, .samples_per_display_frame = FRAME,
	});
	return sp;
}

// one display frame of a tone, both channels alike, then the rendering
static void frame(spectrum_t *sp, unsigned *offset) {
	test_signal_t s = { SIGNAL_SINE, 1000.0, 0, 0.5, 0, 0, 0, 0 };
	float *mono = malloc(sizeof(float) * (*offset + FRAME)), stereo[FRAME * 2];
	test_signal_generate(&s, mono, *offset + FRAME, FS);
	for (unsigned i = 0; i < FRAME; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[*offset + i];
	free(mono);
	*offset += FRAME;
	spectrum_enqueue_audio_data(sp, stereo, sizeof(stereo));
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	spectrum_render(sp, renderer);
	SDL_FlushRenderer(renderer);   // the surface is read directly
}

static unsigned lit_pixels(void) {
	unsigned lit = 0;
	const uint32_t *px = surface->pixels;
	for (int i = 0; i < WIDTH * HEIGHT; i++)
		if ((px[i] & 0xffffff00) != 0) lit++;
	return lit;
}

// The x of each white line two or three pixels wide crossing the row at
// y, left to right: the plot's frame at either end and the decades between.
static unsigned white_lines(int y, int *xs, unsigned max) {
	const uint32_t *px = (const uint32_t *)surface->pixels + (size_t)y * WIDTH;
	unsigned lines = 0;
	int start = -1;
	for (int x = 0; x <= WIDTH; x++) {
		int white = x < WIDTH && (px[x] & 0xffffff00) == 0xffffff00;
		if (white && start < 0) start = x;
		if (!white && start >= 0) {
			if (x - start >= 2 && x - start <= 3 && lines < max) xs[lines++] = start + 1;
			start = -1;
		}
	}
	return lines;
}

// the x of the topmost pixel of the trace's color
static int trace_peak_x(void) {
	const uint32_t *px = surface->pixels;
	for (int y = 0; y < HEIGHT; y++) {
		for (int x = 0; x < WIDTH; x++) {
			Uint8 r, g, b;
			SDL_GetRGB(px[y * WIDTH + x], SDL_GetPixelFormatDetails(surface->format), NULL, &r, &g, &b);
			if (g > 0x40 && r < g / 2 && b < g) return x;
		}
	}
	return -1;
}

// The frame is the shortest fast length that holds the window of a ridge
// resolution_cents wide at the bottom of the range, through the skewed
// window's own width; halving the cents about doubles it.
TEST frame_follows_the_resolution(void) {
	spectrum_t *sp = streaming();
	const spectrum_config_t *c = spectrum_config(sp);
	float w[4096];
	create_window_function(c->window_function, 4096, 0, c->window_skew, w);
	double need = window_bandwidth(w, 4096) * FS / (c->graph_config.min_freq * (pow(2.0, c->resolution_cents / 1200.0) - 1.0));
	unsigned n = spectrum_frame(sp);
	ASSERT_EQ_FMT(fft_fast_length((uint32_t)ceil(need)), n, "%u");
	ASSERT(n >= need && n < need * 1.1);

	spectrum_config_t finer = *c;
	finer.resolution_cents = c->resolution_cents / 2;
	spectrum_set_config(sp, &finer);
	unsigned longer = spectrum_frame(sp);
	ASSERT(longer > 1.9 * n && longer < 2.2 * n);

	spectrum_free(&sp);
	PASS();
}

// A display names its channel as the oscilloscope does: one channel by
// number, every channel joined, nothing for a stream without any.
TEST channels_are_named(void) {
	char buf[32];
	s9_channel_label(0, 2, buf, sizeof buf);
	ASSERT_STR_EQ("CH1", buf);
	s9_channel_label(1, 2, buf, sizeof buf);
	ASSERT_STR_EQ("CH2", buf);
	s9_channel_label(S9_CHANNEL_ALL, 2, buf, sizeof buf);
	ASSERT_STR_EQ("CH1+CH2", buf);
	s9_channel_label(S9_CHANNEL_ALL, 1, buf, sizeof buf);
	ASSERT_STR_EQ("CH1", buf);
	s9_channel_label(0, 0, buf, sizeof buf);
	ASSERT_STR_EQ("", buf);
	PASS();
}

// A configuration that makes no sense falls back to the default.
TEST config_is_kept_sane(void) {
	spectrum_t *sp = spectrum_new(NULL);
	spectrum_config_t c = default_spectrum_config;
	c.resolution_cents = 0;
	c.padding = 9;
	c.graph_config.min_freq = 500;
	c.graph_config.max_freq = 100;
	c.graph_config.min_db = 0;
	c.graph_config.max_db = -10;
	spectrum_set_config(sp, &c);
	const spectrum_config_t *got = spectrum_config(sp);
	ASSERT_EQ_FMT(default_spectrum_config.resolution_cents, got->resolution_cents, "%u");
	ASSERT_EQ_FMT(3u, got->padding, "%u");
	ASSERT_EQ_FMT(default_spectrum_config.graph_config.min_freq, got->graph_config.min_freq, "%u");
	ASSERT_EQ_FMT(default_spectrum_config.graph_config.max_freq, got->graph_config.max_freq, "%u");
	ASSERT_EQ_FMT(default_spectrum_config.graph_config.min_db, got->graph_config.min_db, "%d");
	ASSERT_EQ_FMT(default_spectrum_config.graph_config.max_db, got->graph_config.max_db, "%d");
	spectrum_free(&sp);
	PASS();
}

// A tone through a second of frames lights the plot beyond its graticule,
// and its peak sits on the graticule's line for its frequency: of the
// white lines crossing a row of the plot, the frame's two edges and the
// three decades between them, the middle one is 1 kHz, and the trace is
// highest within a few pixels of it.
TEST a_tone_peaks_at_its_frequency(void) {
	spectrum_t *sp = streaming();
	unsigned offset = 0;
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	spectrum_render(sp, renderer);
	SDL_FlushRenderer(renderer);
	unsigned before = lit_pixels();
	int xs[8];
	unsigned lines = 0;
	for (int y = HEIGHT / 2; y < HEIGHT / 2 + 40 && lines != 5; y++) lines = white_lines(y, xs, 8);
	ASSERT_EQ_FMT(5u, lines, "%u");

	for (int i = 0; i < 60; i++) frame(sp, &offset);
	ASSERT(lit_pixels() > before);
	int peak = trace_peak_x();
	ASSERT(peak >= 0);
	ASSERT_IN_RANGE(xs[2], peak, 3);
	spectrum_free(&sp);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	if (!SDL_Init(SDL_INIT_VIDEO) || !TTF_Init()) {
		fprintf(stderr, "SDL: %s\n", SDL_GetError());
		return 1;
	}
	surface = SDL_CreateSurface(WIDTH, HEIGHT, SDL_PIXELFORMAT_RGBA8888);
	renderer = SDL_CreateSoftwareRenderer(surface);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	GREATEST_MAIN_BEGIN();
	RUN_TEST(frame_follows_the_resolution);
	RUN_TEST(channels_are_named);
	RUN_TEST(config_is_kept_sane);
	RUN_TEST(a_tone_peaks_at_its_frequency);
	GREATEST_MAIN_END();
}
