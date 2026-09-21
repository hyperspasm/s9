// The stereo spectrum end to end: audio in, frames rendered on SDL's
// software renderer. Runs without a display. Each half shows its own
// channel, and one configuration reaches both.
#include "greatest.h"
#include "stereo_spectrum.h"
#include "test_signals.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdlib.h>
#include <string.h>

#define FS 48000
#define FRAME 800          // samples per display frame at 60 Hz
#define WIDTH 1280
#define HEIGHT 720

static SDL_Surface *surface;
static SDL_Renderer *renderer;

static stereo_spectrum_t *streaming(void) {
	stereo_spectrum_t *ss = stereo_spectrum_new(NULL);
	stereo_spectrum_prepare_to_stream(ss, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 2,
		.buffer_frames = 256, .samples_per_display_frame = FRAME,
	});
	return ss;
}

// one display frame of a tone on the left channel and silence on the right, then the rendering
static void frame(stereo_spectrum_t *ss, unsigned *offset) {
	test_signal_t s = { SIGNAL_SINE, 1000.0, 0, 0.5, 0, 0, 0, 0 };
	float *mono = malloc(sizeof(float) * (*offset + FRAME)), stereo[FRAME * 2];
	test_signal_generate(&s, mono, *offset + FRAME, FS);
	for (unsigned i = 0; i < FRAME; i++) { stereo[2 * i] = mono[*offset + i]; stereo[2 * i + 1] = 0.0f; }
	free(mono);
	*offset += FRAME;
	stereo_spectrum_enqueue_audio_data(ss, stereo, sizeof(stereo));
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	stereo_spectrum_render(ss, renderer);
	SDL_RenderFlush(renderer);   // the surface is read directly
}

// lit pixels in the top or the bottom half of the screen
static unsigned lit_pixels(int bottom) {
	unsigned lit = 0;
	const uint32_t *px = (const uint32_t *)surface->pixels + (bottom ? WIDTH * (HEIGHT / 2) : 0);
	for (int i = 0; i < WIDTH * (HEIGHT / 2); i++)
		if ((px[i] & 0xffffff00) != 0) lit++;
	return lit;
}

// A tone on the left channel only lights the top plot beyond its
// graticule and leaves the bottom one at its graticule.
TEST each_half_shows_its_channel(void) {
	stereo_spectrum_t *ss = streaming();
	unsigned offset = 0;
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	stereo_spectrum_render(ss, renderer);
	SDL_RenderFlush(renderer);
	unsigned top = lit_pixels(0), bottom = lit_pixels(1);
	ASSERT(top > 0 && bottom > 0);   // the graticules
	for (int i = 0; i < 60; i++) frame(ss, &offset);
	ASSERT(lit_pixels(0) > top);
	ASSERT_EQ_FMT(bottom, lit_pixels(1), "%u");
	stereo_spectrum_free(&ss);
	PASS();
}

// The configuration reaches both sides: a finer resolution lengthens the
// frame, which both report alike.
TEST configuration_reaches_both(void) {
	stereo_spectrum_t *ss = streaming();
	unsigned before = stereo_spectrum_frame(ss);
	spectrum_config_t c = *stereo_spectrum_config(ss);
	c.resolution_cents /= 2;
	stereo_spectrum_set_config(ss, &c);
	unsigned after = stereo_spectrum_frame(ss);
	ASSERT(after > 1.9 * before && after < 2.2 * before);
	ASSERT_EQ_FMT(c.resolution_cents, stereo_spectrum_config(ss)->resolution_cents, "%u");
	stereo_spectrum_free(&ss);
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
	RUN_TEST(each_half_shows_its_channel);
	RUN_TEST(configuration_reaches_both);
	GREATEST_MAIN_END();
}
