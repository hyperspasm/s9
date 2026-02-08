#ifndef lissajous_h
#define lissajous_h

#include "s9_dsp.h"
#include "s9_colors.h"

#include <SDL2/SDL.h>

typedef struct lissajous lissajous_t;

typedef struct {
	SDL_Color trace_color;
	float trace_width;
	uint8_t persistence;  // 0-255, higher = longer trace persistence
	float gain;           // gain multiplier for both X and Y axes
	float pixel_rate;     // pixels per second for resampling
} lissajous_config_t;

static const lissajous_config_t default_lissajous_config = {
	.trace_color = S9_YELLOW,
	.trace_width = 2.f,
	.persistence = 180,
	.gain = 2.0f,
	.pixel_rate = 176400.0f,
};

// create lissajous
lissajous_t *lissajous_new(const lissajous_config_t *config);

// destroy lissajous
void lissajous_free(lissajous_t **liss);

// render lissajous
void lissajous_render(lissajous_t *liss, SDL_Renderer *renderer);

// prepare for audio streaming
void lissajous_prepare_to_stream(lissajous_t *liss, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void lissajous_enqueue_audio_data(lissajous_t *liss, const void *data, uint32_t size);

#endif /* lissajous_h */
