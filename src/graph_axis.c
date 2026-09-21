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
		CHECK_ERROR(TTF_GetStringSize(font, label, 0, &w, &h), continue, "failed to measure string \"%s\"", label);
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
			SDL_RenderLine(renderer, plot->x, y, plot->x + plot->w, y);

			char label[32];
			graph_format_db(label, sizeof(label), db);
			draw_sdl_render_text_at_position(renderer, font, label, ANCHOR_MIDDLE_RIGHT, axis->major_color,
											 plot->x - label_margin, (int)y);
		} else if (db % (int)axis->minor_interval == 0) {
			SDL_SET_RENDER_DRAW_COLOR(renderer, &axis->minor_color);
			SDL_RenderLine(renderer, plot->x, y, plot->x + GRAPH_DB_AXIS_TICK_LENGTH, y);
		}
	}
}

// -- the frequency axis

#define HZ_MAJOR_WIDTH 2.0f   // the decades' lines

// the decades and the steps between them from min_hz up, calling visit for each with whether it is a decade
static void hz_steps(const graph_hz_axis_t *axis, void (*visit)(void *, float, int), void *ctx) {
	float freq = (float)axis->min_hz;
	while (freq < (float)axis->max_hz) {
		float step = powf(10.f, floorf(log10f(freq)));
		visit(ctx, freq, freq == step);
		freq = step * (1.f + floorf(freq / step));
	}
	float max = (float)axis->max_hz;
	visit(ctx, max, powf(10.f, floorf(log10f(max))) == max);
}

typedef struct { TTF_Font *font; int height, right; float max_hz; } hz_measure_t;

static void measure_step(void *ctx, float freq, int major) {
	hz_measure_t *m = ctx;
	if (!major) return;
	char label[32];
	graph_format_hz(label, sizeof(label), freq);
	int w, h;
	CHECK_ERROR(TTF_GetStringSize(m->font, label, 0, &w, &h), return, "failed to measure string \"%s\"", label);
	if (m->height < h) m->height = h;
	if (freq == m->max_hz && m->right < w / 2) m->right = w / 2;
}

void graph_hz_axis_measure(const graph_hz_axis_t *axis, TTF_Font *font, int *height, int *right) {
	hz_measure_t m = { font, 0, 0, (float)axis->max_hz };
	hz_steps(axis, measure_step, &m);
	*height = m.height;
	*right = m.right;
}

float graph_hz_axis_x(const graph_hz_axis_t *axis, const SDL_Rect *plot, double hz) {
	float log_min = log10f((float)axis->min_hz), log_max = log10f((float)axis->max_hz);
	return plot->x + (log10f((float)hz) - log_min) * ((float)plot->w / (log_max - log_min));
}

typedef struct { const graph_hz_axis_t *axis; SDL_Renderer *renderer; TTF_Font *font; const SDL_Rect *plot; int label_margin; int lines; } hz_draw_t;

static void draw_step(void *ctx, float freq, int major) {
	hz_draw_t *d = ctx;
	float x = graph_hz_axis_x(d->axis, d->plot, freq);
	if (major) {
		if (d->lines) {
			sdl_polygon_t *line = rect_new(x - HZ_MAJOR_WIDTH * 0.5f, d->plot->y - HZ_MAJOR_WIDTH * 0.5f,
			                               HZ_MAJOR_WIDTH, d->plot->h + HZ_MAJOR_WIDTH, d->axis->major_color);
			if (line) {
				SDL_RENDER_GEOMETRY_POLYGON(d->renderer, line);
				sdl_polygon_free(&line);
			}
		}
		char label[32];
		graph_format_hz(label, sizeof(label), freq);
		draw_sdl_render_text_at_position(d->renderer, d->font, label, ANCHOR_TOP_MIDDLE, d->axis->major_color,
		                                 (int)x, d->plot->y + d->plot->h + d->label_margin);
	} else if (d->lines) {
		SDL_SET_RENDER_DRAW_COLOR(d->renderer, &d->axis->minor_color);
		SDL_RenderLine(d->renderer, x, d->plot->y, x, d->plot->y + d->plot->h);
	}
}

void graph_hz_axis_draw(const graph_hz_axis_t *axis, SDL_Renderer *renderer, TTF_Font *font, const SDL_Rect *plot, int label_margin) {
	hz_draw_t d = { axis, renderer, font, plot, label_margin, 1 };
	hz_steps(axis, draw_step, &d);
}

void graph_hz_axis_draw_labels(const graph_hz_axis_t *axis, SDL_Renderer *renderer, TTF_Font *font, const SDL_Rect *plot, int label_margin) {
	hz_draw_t d = { axis, renderer, font, plot, label_margin, 0 };
	hz_steps(axis, draw_step, &d);
}
