#ifndef draw_sdl_h
#define draw_sdl_h

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

// Draws a path with specified width, color, and fill option using SDL. The path can be open or closed and requires at least two points.
void draw_sdl_path(SDL_Renderer *renderer, SDL_Color color, float width, int filled, const SDL_FPoint *path_points, int count);

#endif /* draw_sdl_h */
