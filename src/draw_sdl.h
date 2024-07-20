#ifndef draw_sdl_h
#define draw_sdl_h

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#define SDL_SET_RENDER_DRAW_COLOR(renderer, color) \
	SDL_SetRenderDrawColor((renderer), (color)->r, (color)->g, (color)->b, (color)->a)

// A path is a defined by a series of points.
// When the first and last points of a path are equal it is considered closed.
typedef struct sdl_path sdl_path_t;

// create a new path
sdl_path_t *sdl_path_new(SDL_Color *color, float width, int filled, const SDL_FPoint *path_points, int count);

// free a path
void sdl_path_free(sdl_path_t **path);

// render the path
int sdl_path_render(sdl_path_t *path, SDL_Renderer *renderer);


// Renders text to an SDL texture, updating the texture and optionally outputting the text dimensions.
int draw_sdl_render_text(SDL_Renderer *renderer, SDL_Texture **texture, TTF_Font *font, SDL_Color *color, const char *text, int *out_width, int *out_height);

#endif /* draw_sdl_h */
