#ifndef fps_h
#define fps_h

#include "s9.h"

#include <SDL2/SDL.h>

typedef struct fps fps_t;

// create fps measurement context
fps_t *fps_new(SDL_Color color, uint32_t interval_ms);

// destroy fps measurement context
void fps_free(fps_t **fps);

// renders the current measurement at the top left corner of the viewport
void fps_render(fps_t *fps, SDL_Renderer *renderer);

#endif /* fps_h */
