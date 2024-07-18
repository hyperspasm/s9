#ifndef template_h
#define template_h

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

typedef struct template template_t;

// create template
template_t *template_new(void);

// destroy template
void template_free(template_t **tp);

// render template
void template_render(template_t *tp, SDL_Renderer *renderer);

#endif /* template_h */
