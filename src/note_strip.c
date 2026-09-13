#include "note_strip.h"
#include "s9.h"
#include "s9_font.h"
#include "draw_sdl.h"
#include "label.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define FONT_SIZE 16
#define MARGIN 10               // between the strip and the viewport's sides
#define TEXT_MARGIN 8           // between the text and the scale
#define SPAN_CENTS 50.0f        // the scale reaches this far either side of the note: to the next one
#define MINOR_TICK 4.0f         // half of a tick's height, pixels
#define MAJOR_TICK 8.0f
#define MARKER_HEIGHT 22.0f

struct note_strip {
	note_strip_config_t config;

	SDL_Renderer *renderer;
	SDL_Rect viewport;
	TTF_Font *font;
	int text_reserve;           // the widest text's width: the scale starts past it whatever the note
	label_t text;
};

const note_strip_config_t *note_strip_config(const note_strip_t *s) {
	return &s->config;
}

void note_strip_set_config(note_strip_t *s, const note_strip_config_t *config) {
	s->config = config ? *config : default_note_strip_config;
	if (!(s->config.tick_cents > 0.0f)) s->config.tick_cents = default_note_strip_config.tick_cents;
	if (!(s->config.marker_width >= 1.0f)) s->config.marker_width = default_note_strip_config.marker_width;
}

static void free_drawing(note_strip_t *s) {
	label_free(&s->text);
	if (s->font) { TTF_CloseFont(s->font); s->font = NULL; }
	s->renderer = NULL;
}

note_strip_t *note_strip_new(const note_strip_config_t *config) {
	note_strip_t *s = malloc(sizeof(note_strip_t));
	CHECK_ALLOC(s, return NULL);
	memset(s, 0, sizeof(*s));
	note_strip_set_config(s, config);
	return s;
}

void note_strip_free(note_strip_t **s) {
	if (s && *s) {
		free_drawing(*s);
		free(*s);
		*s = NULL;
	}
}

static void prepare_to_draw(note_strip_t *s, SDL_Renderer *renderer) {
	SDL_Rect viewport;
	SDL_RenderGetViewport(renderer, &viewport);
	if (s->renderer == renderer && SDL_RectEquals(&s->viewport, &viewport)) return;

	free_drawing(s);
	s->renderer = renderer;
	s->viewport = viewport;
	s->font = s9_open_font(FONT_SIZE);
	CHECK_ERROR(s->font != NULL, return, "unable to create font");
	int w = 0;
	TTF_SizeUTF8(s->font, "G#-1 -50", &w, NULL);   // as wide as a name and its cents get
	s->text_reserve = w;
}

void note_strip_render(note_strip_t *s, SDL_Renderer *renderer, const note_t *note, bool valid) {
	prepare_to_draw(s, renderer);
	if (!s->font) return;
	const note_strip_config_t *c = &s->config;
	float y = s->viewport.h / 2.0f;
	float x0 = (float)(MARGIN + s->text_reserve + TEXT_MARGIN);
	float x1 = (float)(s->viewport.w - MARGIN);
	if (x1 - x0 < 2.0f) return;
	float per_cent = (x1 - x0) / (2.0f * SPAN_CENTS);

	// the scale: its line and its ticks, taller at the note and at the ends
	SDL_SET_RENDER_DRAW_COLOR(renderer, &c->minor_color);
	int ticks = (int)lroundf(2.0f * SPAN_CENTS / c->tick_cents);
	for (int i = 0; i <= ticks; i++) {
		float x = x0 + i * c->tick_cents * per_cent;
		if (x > x1) break;
		SDL_RenderDrawLineF(renderer, x, y - MINOR_TICK, x, y + MINOR_TICK);
	}
	SDL_SET_RENDER_DRAW_COLOR(renderer, &c->major_color);
	SDL_RenderDrawLineF(renderer, x0, y, x1, y);
	for (int i = -1; i <= 1; i++) {
		float x = x0 + (i + 1) * SPAN_CENTS * per_cent;
		SDL_RenderDrawLineF(renderer, x, y - MAJOR_TICK, x, y + MAJOR_TICK);
	}

	if (!note) {
		label_set(&s->text, renderer, s->font, c->readout_color, "");
		return;
	}

	// the note and its cents as text, and the marker at the cents
	char name[NOTE_NAME_MAX], text[LABEL_TEXT];
	note_name(note->number, name, sizeof(name));
	int cents = (int)lrintf(note->cents);
	if (cents == 0) snprintf(text, sizeof(text), "%s 0", name);
	else snprintf(text, sizeof(text), "%s %+d", name, cents);
	label_set(&s->text, renderer, s->font, valid ? c->readout_color : c->held_color, text);
	label_blit(&s->text, renderer, (float)MARGIN, y, ANCHOR_MIDDLE_LEFT);

	SDL_Color marker = valid ? c->marker_color : c->held_color;
	SDL_SET_RENDER_DRAW_COLOR(renderer, &marker);
	float x = x0 + (note->cents + SPAN_CENTS) * per_cent;
	SDL_FRect rect = { x - c->marker_width / 2.0f, y - MARKER_HEIGHT / 2.0f, c->marker_width, MARKER_HEIGHT };
	SDL_RenderFillRectF(renderer, &rect);
}
