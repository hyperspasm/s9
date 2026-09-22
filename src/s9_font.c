#include "s9_font.h"
#include "s9.h"
#include "Roboto-Regular.h"

#include <SDL3/SDL.h>

TTF_Font *s9_open_font(int point_size) {
	SDL_IOStream *io = SDL_IOFromConstMem(Roboto_Regular_TTF, sizeof(Roboto_Regular_TTF));
	CHECK_ERROR(io != NULL, return NULL, "SDL_IOFromConstMem failed: %s", SDL_GetError());

	TTF_Font *font = TTF_OpenFontIO(io, true, (float)point_size);
	CHECK_ERROR(font != NULL, return NULL, "TTF_OpenFontIO failed: %s", SDL_GetError());

	return font;
}
