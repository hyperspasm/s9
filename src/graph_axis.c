#include "graph_axis.h"
#include "draw_sdl.h"
#include "s9.h"

#include <math.h>
#include <stdio.h>

void graph_format_hz(char *buf, size_t size, double hz) {
	static const struct { double unit; const char *name; } units[] = {
		{ 1e9, "GHz" }, { 1e6, "MHz" }, { 1e3, "KHz" }, { 1.0, "Hz" },
	};
	for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
		if (hz >= units[i].unit || units[i].unit == 1.0) {
			double value = hz / units[i].unit;
			if (value == floor(value)) snprintf(buf, size, "%d %s", (int)value, units[i].name);
			else snprintf(buf, size, "%0.3f %s", value, units[i].name);
			return;
		}
	}
}

void graph_format_db(char *buf, size_t size, int db) {
	snprintf(buf, size, "%d dB", db);
}

void graph_db_axis_measure(const graph_db_axis_t *axis, TTF_Font *font, int *width, int *height) {
	*width = 0;
	*height = 0;
	for (int db = axis->max_db; db >= axis->min_db; db--) {
		if (db % (int)axis->major_interval) continue;
		char label[32];
		graph_format_db(label, sizeof(label), db);
		int w, h;
		CHECK_ERROR(TTF_SizeUTF8(font, label, &w, &h) == 0, continue, "failed to measure string \"%s\"", label);
		if (*width < w) *width = w;
		if (*height < h) *height = h;
	}
}

float graph_db_axis_y(const graph_db_axis_t *axis, const SDL_Rect *plot, float db) {
	float scale = (float)plot->h / (float)(axis->max_db - axis->min_db);
	return plot->y + ((float)axis->max_db - db) * scale;
}

void graph_db_axis_draw(const graph_db_axis_t *axis, SDL_Renderer *renderer, TTF_Font *font, const SDL_Rect *plot, int label_margin) {
	for (int db = axis->max_db; db >= axis->min_db; db--) {
		float y = graph_db_axis_y(axis, plot, (float)db);
		if (db % (int)axis->major_interval == 0) {
			SDL_SET_RENDER_DRAW_COLOR(renderer, &axis->minor_color);
			SDL_RenderDrawLineF(renderer, plot->x, y, plot->x + plot->w, y);

			char label[32];
			graph_format_db(label, sizeof(label), db);
			draw_sdl_render_text_at_position(renderer, font, label, ANCHOR_MIDDLE_RIGHT, axis->major_color,
											 plot->x - label_margin, (int)y);
		} else if (db % (int)axis->minor_interval == 0) {
			SDL_SET_RENDER_DRAW_COLOR(renderer, &axis->minor_color);
			SDL_RenderDrawLineF(renderer, plot->x, y, plot->x + GRAPH_DB_AXIS_TICK_LENGTH, y);
		}
	}
}
