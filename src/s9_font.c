#include "s9_font.h"
#include "s9.h"
#include "Roboto-Regular.h"

#include <SDL2/SDL.h>

TTF_Font *s9_open_font(int point_size) {
	SDL_RWops *rw = SDL_RWFromConstMem(Roboto_Regular_TTF, sizeof(Roboto_Regular_TTF));
	CHECK_ERROR(rw != NULL, return NULL, "SDL_RWFromConstMem failed: %s", SDL_GetError());

	TTF_Font *font = TTF_OpenFontRW(rw, 1, point_size);
	CHECK_ERROR(font != NULL, return NULL, "TTF_OpenFontRW failed: %s", TTF_GetError());

	return font;
}
