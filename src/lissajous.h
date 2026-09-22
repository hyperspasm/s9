#ifndef lissajous_h
#define lissajous_h

#include "s9_dsp.h"
#include "s9_colors.h"

#include <SDL3/SDL.h>

typedef struct lissajous lissajous_t;

typedef struct {
	SDL_Color trace_color;
	float trace_width;
	uint8_t persistence;  // 0-255, higher = longer trace persistence
} lissajous_config_t;

static const lissajous_config_t default_lissajous_config = {
	.trace_color = S9_LEMON,
	.trace_width = 2.f,
	.persistence = 180,
};

// create lissajous
lissajous_t *lissajous_new(const lissajous_config_t *config);

// destroy lissajous
void lissajous_free(lissajous_t **liss);

// render lissajous
void lissajous_render(lissajous_t *liss, SDL_Renderer *renderer);

// settings
void lissajous_set_amp_per_div(lissajous_t *liss, float amplitude);
float lissajous_get_amp_per_div(const lissajous_t *liss);
void lissajous_set_upsampling(lissajous_t *liss, float factor);
float lissajous_get_upsampling(const lissajous_t *liss);

// stop audio streaming
void lissajous_stop_streaming(lissajous_t *liss);

// prepare for audio streaming
void lissajous_prepare_to_stream(lissajous_t *liss, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void lissajous_enqueue_audio_data(lissajous_t *liss, const void *data, uint32_t size);

#endif /* lissajous_h */
