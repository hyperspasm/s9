#ifndef strip_h
#define strip_h

#include "s9_colors.h"

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

// A strip: text at the left, and to its right a scale with ticks, some of
// them major and labelled, and a marker. Positions along the scale are
// fractions, 0 at its left end and 1 at its right; what the scale measures
// is the caller's. It draws across the viewport it is given, which should
// be STRIP_HEIGHT high, and every strip starts its scale the same distance
// in from the left, so strips stacked line up.

#define STRIP_HEIGHT 44        // the band it lays itself out in, pixels
#define STRIP_TICKS_MAX 128
#define STRIP_LABEL_MAX 8

typedef struct {
	float position;
	bool major;                     // taller, and the kind that carries a label
	char label[STRIP_LABEL_MAX];    // under the tick; empty for none
} strip_tick_t;

typedef struct {
	float marker_width;             // pixels
	SDL_Color major_color;          // the scale's line, its major ticks, and their labels
	SDL_Color minor_color;          // the minor ticks
	SDL_Color marker_color;         // while the reading is valid
	SDL_Color readout_color;        // the text while the reading is valid
	SDL_Color held_color;           // the text and the marker while the reading only holds
} strip_config_t;

static const strip_config_t default_strip_config = {
	.marker_width = 6.0f,
	.major_color = S9_WHITE,
	.minor_color = (SDL_Color){ .r = 0xff, .g = 0xff, .b = 0xff, .a = 0x80 },
	.marker_color = S9_LEMON,
	.readout_color = S9_WHITE,
	.held_color = S9_SLATE,
};

typedef struct strip strip_t;

// create a strip; NULL takes the default configuration
strip_t *strip_new(const strip_config_t *config);

// destroy a strip
void strip_free(strip_t **s);

// the configuration, and its replacement
const strip_config_t *strip_config(const strip_t *s);
void strip_set_config(strip_t *s, const strip_config_t *config);

// Draw into the current viewport: the text, empty for none; the ticks; and
// the marker at a position, or none when the position is negative. The
// reading is valid now, or held from before.
void strip_render(strip_t *s, SDL_Renderer *renderer, const char *text, const strip_tick_t *ticks, uint32_t count,
                  float marker, bool valid);

#endif /* strip_h */
