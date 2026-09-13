#ifndef note_strip_h
#define note_strip_h

#include "note.h"
#include "s9_colors.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

// A tuner's strip: a note's name and how many cents off it is, as text at
// the left, and that distance as a marker on a scale of cents that reaches
// half a semitone either way, ticked. It draws across the viewport it is
// given, which should be NOTE_STRIP_HEIGHT high. Without a note it draws
// the scale alone.

#define NOTE_STRIP_HEIGHT 40   // the band it lays itself out in, pixels

typedef struct note_strip note_strip_t;

typedef struct {
	float tick_cents;          // between the scale's ticks
	float marker_width;        // pixels
	SDL_Color major_color;     // the scale's line, and its ticks at the note and at the ends
	SDL_Color minor_color;     // the ticks between
	SDL_Color marker_color;    // while the note is valid
	SDL_Color readout_color;   // the text while the note is valid
	SDL_Color held_color;      // the text and the marker while it only holds
} note_strip_config_t;

static const note_strip_config_t default_note_strip_config = {
	.tick_cents = 10.0f,
	.marker_width = 6.0f,
	.major_color = S9_WHITE,
	.minor_color = (SDL_Color){ .r = 0xff, .g = 0xff, .b = 0xff, .a = 0x80 },
	.marker_color = S9_LEMON,
	.readout_color = S9_WHITE,
	.held_color = S9_SLATE,
};

// create a strip; NULL takes the default configuration
note_strip_t *note_strip_new(const note_strip_config_t *config);

// destroy a strip
void note_strip_free(note_strip_t **s);

// the configuration, and its replacement
const note_strip_config_t *note_strip_config(const note_strip_t *s);
void note_strip_set_config(note_strip_t *s, const note_strip_config_t *config);

// draw into the current viewport: the note, or NULL for none, and whether
// it is valid now rather than held from before
void note_strip_render(note_strip_t *s, SDL_Renderer *renderer, const note_t *note, bool valid);

#endif /* note_strip_h */
