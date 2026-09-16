#ifndef label_h
#define label_h

#include "draw_sdl.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

// Text drawn from a texture that is remade only when the text or its
// color changes. Caller-owned and zeroed to begin with; free it before it
// goes away or the renderer does.

#define LABEL_TEXT 64

typedef struct {
	SDL_Texture *texture;
	int w, h;
	SDL_Color color;
	char text[LABEL_TEXT];
} label_t;

// the text to show, in a color; empty text shows nothing
void label_set(label_t *l, SDL_Renderer *renderer, TTF_Font *font, SDL_Color color, const char *text);

// draw it anchored at a point
void label_blit(const label_t *l, SDL_Renderer *renderer, float x, float y, anchor_position_t anchor);

void label_free(label_t *l);

#endif /* label_h */
