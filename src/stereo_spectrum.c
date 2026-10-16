#include "stereo_spectrum.h"
#include "s9.h"

#include <stdlib.h>

struct stereo_spectrum {
	spectrum_config_t config;
	spectrum_t *left;
	spectrum_t *right;
};

// the configuration for a side
static spectrum_config_t side(const spectrum_config_t *config, uint32_t channel) {
	spectrum_config_t c = *config;
	c.channel = channel;
	return c;
}

const spectrum_config_t *stereo_spectrum_config(const stereo_spectrum_t *ss) {
	return &ss->config;
}

void stereo_spectrum_set_config(stereo_spectrum_t *ss, const spectrum_config_t *config) {
	ss->config = config ? *config : default_spectrum_config;
	spectrum_config_t c = side(&ss->config, 0);
	spectrum_set_config(ss->left, &c);
	c = side(&ss->config, 1);
	spectrum_set_config(ss->right, &c);
	ss->config = *spectrum_config(ss->left);   // as the spectrum keeps it sane
	ss->config.channel = config ? config->channel : default_spectrum_config.channel;
}

stereo_spectrum_t *stereo_spectrum_new(const spectrum_config_t *config) {
	stereo_spectrum_t *ss = malloc(sizeof(stereo_spectrum_t));
	CHECK_ALLOC(ss, return NULL);
	memset(ss, 0, sizeof(*ss));
	ss->config = config ? *config : default_spectrum_config;
	spectrum_config_t c = side(&ss->config, 0);
	ss->left = spectrum_new(&c);
	CHECK_ALLOC(ss->left, stereo_spectrum_free(&ss); return NULL);
	c = side(&ss->config, 1);
	ss->right = spectrum_new(&c);
	CHECK_ALLOC(ss->right, stereo_spectrum_free(&ss); return NULL);
	return ss;
}

void stereo_spectrum_free(stereo_spectrum_t **ss) {
	if (ss && *ss) {
		spectrum_free(&(*ss)->left);
		spectrum_free(&(*ss)->right);
		free(*ss);
		*ss = NULL;
	}
}

void stereo_spectrum_render(stereo_spectrum_t *ss, SDL_Renderer *renderer) {
	SDL_Rect viewport, half;
	SDL_GetRenderViewport(renderer, &viewport);
	half = viewport;
	half.h = viewport.h / 2;
	SDL_SetRenderViewport(renderer, &half);
	spectrum_render(ss->left, renderer);
	half.y += half.h;
	SDL_SetRenderViewport(renderer, &half);
	spectrum_render(ss->right, renderer);
	SDL_SetRenderViewport(renderer, &viewport);
}

uint32_t stereo_spectrum_frame(const stereo_spectrum_t *ss) {
	return spectrum_frame(ss->left);
}

void stereo_spectrum_stop_streaming(stereo_spectrum_t *ss) {
	spectrum_stop_streaming(ss->left);
	spectrum_stop_streaming(ss->right);
}

void stereo_spectrum_prepare_to_stream(stereo_spectrum_t *ss, const s9_audio_stream_config_t *stream_config) {
	spectrum_prepare_to_stream(ss->left, stream_config);
	spectrum_prepare_to_stream(ss->right, stream_config);
}

void stereo_spectrum_enqueue_audio_data(stereo_spectrum_t *ss, const void *data, uint32_t size) {
	spectrum_enqueue_audio_data(ss->left, data, size);
	spectrum_enqueue_audio_data(ss->right, data, size);
}
