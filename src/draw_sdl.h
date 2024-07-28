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

// create a new path from an SDL_FRect
sdl_path_t *sdl_path_new_from_frect(SDL_Color *color, float width, int filled, SDL_FRect rect);

// create a new path from an SDL_Rect
sdl_path_t *sdl_path_new_from_rect(SDL_Color *color, float width, int filled, SDL_Rect rect);

// free a path
void sdl_path_free(sdl_path_t **path);

// render the path
int sdl_path_render(sdl_path_t *path, SDL_Renderer *renderer);

typedef enum {
	POSITION_TOP_LEFT,
	POSITION_TOP_MIDDLE,
	POSITION_TOP_RIGHT,
	POSITION_MIDDLE_LEFT,
	POSITION_MIDDLE_RIGHT,
	POSITION_BOTTOM_LEFT,
	POSITION_BOTTOM_MIDDLE,
	POSITION_BOTTOM_RIGHT,
	POSITION_MIDDLE,
} rect_edge_position_t;

// renders text at specified position with and alignment
int draw_sdl_render_text_at_position(SDL_Renderer *renderer, TTF_Font *font, const char *text, rect_edge_position_t anchor, SDL_Color color, int x, int y);

// renders text to an SDL texture, updating the texture and optionally outputting the text dimensions.
int draw_sdl_render_text(SDL_Renderer *renderer, SDL_Texture **texture, TTF_Font *font, SDL_Color *color, const char *text, int *out_width, int *out_height);

// inset a rect
SDL_Rect draw_sdl_inset_rect(SDL_Rect rect, float inset);

// clear the current viewport with a given background color, or black in color is NULL
void draw_sdl_clear_viewport(SDL_Renderer *renderer, SDL_Color *color);

#endif /* draw_sdl_h */
