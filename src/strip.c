#include "strip.h"
#include "s9.h"
#include "s9_font.h"
#include "draw_sdl.h"
#include "label.h"

#include <math.h>
#include <stdlib.h>

#define FONT_SIZE 16
#define MARGIN 10               // between the strip and the viewport's sides
#define TEXT_COLUMN 96          // the text's column, pixels: the scale starts past it
#define LINE_Y_LABELLED 14.0f   // the scale's line, from the top, when labels hang under its ticks
#define MINOR_TICK 4.0f         // half of a tick's height, pixels
#define MAJOR_TICK 8.0f
#define LABEL_GAP 2.0f          // between a major tick and its label
#define MARKER_HEIGHT 22.0f

struct strip {
	strip_config_t config;

	SDL_Renderer *renderer;
	SDL_Rect viewport;
	TTF_Font *font;
	label_t text;
	label_t labels[STRIP_TICKS_MAX];   // the ticks' labels, by tick
};

const strip_config_t *strip_config(const strip_t *s) {
	return &s->config;
}

void strip_set_config(strip_t *s, const strip_config_t *config) {
	s->config = config ? *config : default_strip_config;
	if (!(s->config.marker_width >= 1.0f)) s->config.marker_width = default_strip_config.marker_width;
	if (!(s->config.dim_marker_width >= 1.0f)) s->config.dim_marker_width = default_strip_config.dim_marker_width;
}

static void free_drawing(strip_t *s) {
	label_free(&s->text);
	for (int i = 0; i < STRIP_TICKS_MAX; i++) label_free(&s->labels[i]);
	if (s->font) { TTF_CloseFont(s->font); s->font = NULL; }
	s->renderer = NULL;
}

strip_t *strip_new(const strip_config_t *config) {
	strip_t *s = malloc(sizeof(strip_t));
	CHECK_ALLOC(s, return NULL);
	memset(s, 0, sizeof(*s));
	strip_set_config(s, config);
	return s;
}

void strip_free(strip_t **s) {
	if (s && *s) {
		free_drawing(*s);
		free(*s);
		*s = NULL;
	}
}

static void prepare_to_draw(strip_t *s, SDL_Renderer *renderer) {
	SDL_Rect viewport;
	SDL_GetRenderViewport(renderer, &viewport);
	if (s->renderer == renderer && SDL_RectsEqual(&s->viewport, &viewport)) return;

	free_drawing(s);
	s->renderer = renderer;
	s->viewport = viewport;
	s->font = s9_open_font(FONT_SIZE * draw_sdl_scale(renderer));
	CHECK_ERROR(s->font != NULL, return, "unable to create font");
}

void strip_render(strip_t *s, SDL_Renderer *renderer, const char *text, const strip_tick_t *ticks, uint32_t count,
                  float marker, bool valid, float dim) {
	prepare_to_draw(s, renderer);
	if (!s->font) return;
	const strip_config_t *c = &s->config;
	if (count > STRIP_TICKS_MAX) count = STRIP_TICKS_MAX;

	bool labelled = false;
	for (uint32_t i = 0; i < count; i++)
		if (ticks[i].label[0]) labelled = true;
	float scale = draw_sdl_scale(renderer);
	float y = labelled ? LINE_Y_LABELLED * scale : s->viewport.h / 2.0f;
	float x0 = (MARGIN + TEXT_COLUMN) * scale;
	float x1 = (float)s->viewport.w - MARGIN * scale;
	if (x1 - x0 < 2.0f) return;

	// the minor ticks, then the line and the major ticks over them, labelled
	SDL_SET_RENDER_DRAW_COLOR(renderer, &c->minor_color);
	for (uint32_t i = 0; i < count; i++) {
		if (ticks[i].major || ticks[i].position < 0.0f || ticks[i].position > 1.0f) continue;
		float x = x0 + ticks[i].position * (x1 - x0);
		draw_sdl_line(renderer, x, y - MINOR_TICK * scale, x, y + MINOR_TICK * scale);
	}
	SDL_SET_RENDER_DRAW_COLOR(renderer, &c->major_color);
	draw_sdl_line(renderer, x0, y, x1, y);
	for (uint32_t i = 0; i < count; i++) {
		if (!ticks[i].major || ticks[i].position < 0.0f || ticks[i].position > 1.0f) continue;
		float x = x0 + ticks[i].position * (x1 - x0);
		draw_sdl_line(renderer, x, y - MAJOR_TICK * scale, x, y + MAJOR_TICK * scale);
		label_set(&s->labels[i], renderer, s->font, c->major_color, ticks[i].label);
		// a label at either end is pulled in to stay whole
		float half = s->labels[i].w / 2.0f, lx = x;
		if (lx + half > (float)s->viewport.w) lx = (float)s->viewport.w - half;
		if (lx - half < 0.0f) lx = half;
		label_blit(&s->labels[i], renderer, lx, y + (MAJOR_TICK + LABEL_GAP) * scale, ANCHOR_TOP_MIDDLE);
	}

	// the text in its column, and the markers on the scale, the dim one under the other
	label_set(&s->text, renderer, s->font, valid ? c->readout_color : c->held_color, text ? text : "");
	label_blit(&s->text, renderer, MARGIN * scale, y, ANCHOR_MIDDLE_LEFT);
	if (dim >= 0.0f) {
		if (dim > 1.0f) dim = 1.0f;
		SDL_SET_RENDER_DRAW_COLOR(renderer, &c->dim_marker_color);
		float x = x0 + dim * (x1 - x0);
		SDL_FRect rect = { x - c->dim_marker_width * scale / 2.0f, y - MARKER_HEIGHT * scale / 2.0f, c->dim_marker_width * scale, MARKER_HEIGHT * scale };
		SDL_RenderFillRect(renderer, &rect);
	}
	if (marker >= 0.0f) {
		if (marker > 1.0f) marker = 1.0f;
		SDL_Color color = valid ? c->marker_color : c->held_color;
		SDL_SET_RENDER_DRAW_COLOR(renderer, &color);
		float x = x0 + marker * (x1 - x0);
		SDL_FRect rect = { x - c->marker_width * scale / 2.0f, y - MARKER_HEIGHT * scale / 2.0f, c->marker_width * scale, MARKER_HEIGHT * scale };
		SDL_RenderFillRect(renderer, &rect);
	}
}
