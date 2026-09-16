#ifndef graph_axis_h
#define graph_axis_h

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <stddef.h>
#include <stdint.h>

// The axes of a graph: the dB axis up the left edge of a plot rectangle,
// the log frequency axis along its bottom, and the labels' formats.

typedef struct {
	int32_t min_db, max_db;
	uint32_t major_interval;   // labelled lines across the plot, in dB
	uint32_t minor_interval;   // ticks at the left edge, in dB
	SDL_Color major_color;     // labels
	SDL_Color minor_color;     // lines and ticks
} graph_db_axis_t;

// "20 Hz", "1 KHz", "1.500 KHz": a frequency with its unit, into buf
void graph_format_hz(char *buf, size_t size, double hz);
void graph_format_db(char *buf, size_t size, int db);

// the room the labels need: the widest label and the label height
void graph_db_axis_measure(const graph_db_axis_t *axis, TTF_Font *font, int *width, int *height);

// how far the minor ticks reach into the plot from its left edge, pixels
#define GRAPH_DB_AXIS_TICK_LENGTH 6

// the y of a dB value within a plot rectangle: max_db at the top
float graph_db_axis_y(const graph_db_axis_t *axis, const SDL_Rect *plot, float db);

// lines, ticks, and labels; labels end label_margin left of the plot
void graph_db_axis_draw(const graph_db_axis_t *axis, SDL_Renderer *renderer, TTF_Font *font, const SDL_Rect *plot, int label_margin);

// The frequency axis along the bottom of a plot: log frequency from min_hz
// at the left edge to max_hz at the right, the decades as lines across the
// plot labelled below it, the steps between them as lines too.
typedef struct {
	double min_hz, max_hz;
	SDL_Color major_color;     // the decades' lines and labels
	SDL_Color minor_color;     // the steps' lines
} graph_hz_axis_t;

// the room the labels need: their height below the plot, and how far the
// last one reaches past the plot's right edge
void graph_hz_axis_measure(const graph_hz_axis_t *axis, TTF_Font *font, int *height, int *right);

// the x of a frequency within a plot rectangle
float graph_hz_axis_x(const graph_hz_axis_t *axis, const SDL_Rect *plot, double hz);

// lines and labels; labels start label_margin below the plot
void graph_hz_axis_draw(const graph_hz_axis_t *axis, SDL_Renderer *renderer, TTF_Font *font, const SDL_Rect *plot, int label_margin);

// the labels alone, for a plot shown without its grid
void graph_hz_axis_draw_labels(const graph_hz_axis_t *axis, SDL_Renderer *renderer, TTF_Font *font, const SDL_Rect *plot, int label_margin);

#endif /* graph_axis_h */
