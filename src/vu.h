#ifndef vu_h
#define vu_h

#include "s9_dsp.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

typedef struct vu vu_t;

// create vu
vu_t *vu_new(void);

// destroy vu
void vu_free(vu_t **tp);

// render vu
void vu_render(vu_t *vu, SDL_Renderer *renderer);

// prepare for audio streaming
void vu_prepare_to_stream(vu_t *vu, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void vu_enqueue_audio_data(vu_t *vu, const void *data, uint32_t size);

#endif /* vu_h */
