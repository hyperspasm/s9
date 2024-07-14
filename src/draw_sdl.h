#ifndef draw_sdl_h
#define draw_sdl_h

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

// Draws a path with specified width, color, and fill option using SDL. The path can be open or closed and requires at least two points.
int draw_sdl_path(SDL_Renderer *renderer, SDL_Color color, float width, int filled, const SDL_FPoint *path_points, int count);

// Renders text to an SDL texture, updating the texture and optionally outputting the text dimensions.
int draw_sdl_render_text(SDL_Renderer *renderer, SDL_Texture **texture, TTF_Font *font, SDL_Color *color, const char *text, int *out_width, int *out_height);

#endif /* draw_sdl_h */
