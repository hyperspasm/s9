#include "label.h"

#include <stdio.h>
#include <string.h>

static int same_color(SDL_Color a, SDL_Color b) {
	return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

void label_free(label_t *l) {
	if (l->texture) SDL_DestroyTexture(l->texture);
	l->texture = NULL;
	l->text[0] = '\0';
}

void label_set(label_t *l, SDL_Renderer *renderer, TTF_Font *font, SDL_Color color, const char *text) {
	if (strcmp(l->text, text) == 0 && same_color(l->color, color)) return;
	label_free(l);
	snprintf(l->text, sizeof(l->text), "%s", text);
	l->color = color;
	if (!text[0]) return;
	draw_sdl_render_text(renderer, &l->texture, font, &color, text, &l->w, &l->h);
}

void label_blit(const label_t *l, SDL_Renderer *renderer, float x, float y, anchor_position_t anchor) {
	if (l->texture) draw_sdl_blit_texture(renderer, l->texture, x, y, anchor);
}
