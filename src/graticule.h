#ifndef graticule_h
#define graticule_h

#include <SDL2/SDL.h>

#include "s9_colors.h"

typedef struct graticule graticule_t;

typedef struct {
	SDL_Color major_color;
	SDL_Color minor_color;
} graticule_config_t;

static const graticule_config_t default_graticule_config = {
	.major_color = S9_WHITE,
	.minor_color = (SDL_Color){ .r = 0xff, .g = 0xff, .b = 0xff, .a = 0x80 },
};

// create graticule
graticule_t *graticule_new(const graticule_config_t *config);

// destroy graticule
void graticule_free(graticule_t **grat);

// set label text (NULL to clear)
void graticule_set_label_bottom(graticule_t *grat, const char *label);
void graticule_set_label_top(graticule_t *grat, const char *label);

// render graticule in the given rect
void graticule_render(graticule_t *grat, SDL_Renderer *renderer, const SDL_Rect *rect);

#endif /* graticule_h */
