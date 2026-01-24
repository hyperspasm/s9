#ifndef oscilloscope_h
#define oscilloscope_h

#include "s9_dsp.h"
#include "window_function.h"
#include "s9_colors.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

typedef struct oscilloscope oscilloscope_t;

typedef struct {
	SDL_Color trace_color;
	float trace_width;
} oscilloscope_config_t;

static const oscilloscope_config_t default_oscilloscope_config = {
	.trace_color = S9_GREEN,
	.trace_width = 2.f,
};

// create oscilloscope
oscilloscope_t *oscilloscope_new(const oscilloscope_config_t *config);

// destroy oscilloscope
void oscilloscope_free(oscilloscope_t **osc);

// render oscilloscope
void oscilloscope_render(oscilloscope_t *osc, SDL_Renderer *renderer);

// prepare for audio streaming
void oscilloscope_prepare_to_stream(oscilloscope_t *osc, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void oscilloscope_enqueue_audio_data(oscilloscope_t *osc, const void *data, uint32_t size);

#endif /* oscilloscope_h */
