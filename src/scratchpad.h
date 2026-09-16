#ifndef scratchpad_h
#define scratchpad_h

#include <SDL2/SDL.h>

typedef struct scratchpad scratchpad_t;

// create scratchpad
scratchpad_t *scratchpad_new(void);

// destroy scratchpad
void scratchpad_free(scratchpad_t **sp);

// render scratchpad
void scratchpad_render(scratchpad_t *sp, SDL_Renderer *renderer);

#endif /* scratchpad_h */
