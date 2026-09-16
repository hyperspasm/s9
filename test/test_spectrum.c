// The spectrum end to end: audio in, frames rendered on SDL's software
// renderer. Runs without a display. The frame follows the resolution at
// the bottom of the range, a configuration that makes no sense falls back
// to the default, and a tone lights the plot. What the bins hold is tested
// on the analyzer.
#include "greatest.h"
#include "spectrum.h"
#include "fft_analyzer.h"
#include "test_signals.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
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
}

static unsigned lit_pixels(void) {
	unsigned lit = 0;
	const uint32_t *px = surface->pixels;
	for (int i = 0; i < WIDTH * HEIGHT; i++)
		if ((px[i] & 0xffffff00) != 0) lit++;
	return lit;
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
	ASSERT_EQ_FMT(fft_analyzer_fast_length((uint32_t)ceil(need)), n, "%u");
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

// A tone through a second of frames lights the plots beyond their graticules.
TEST a_tone_lights_the_plot(void) {
	spectrum_t *sp = streaming();
	unsigned offset = 0;
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	spectrum_render(sp, renderer);
	unsigned before = lit_pixels();
	for (int i = 0; i < 60; i++) frame(sp, &offset);
	ASSERT(lit_pixels() > before);
	spectrum_free(&sp);
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
	RUN_TEST(frame_follows_the_resolution);
	RUN_TEST(channels_are_named);
	RUN_TEST(config_is_kept_sane);
	RUN_TEST(a_tone_lights_the_plot);
	GREATEST_MAIN_END();
}
