#ifndef tuner_strip_h
#define tuner_strip_h

#include "note.h"
#include "strip.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

// A tuner's strip: a note's name and how many cents off it is, to a tenth,
// as text, and the cents on a scale of half a semitone either way, ticked
// and numbered at the ends and at the note. A strip, drawn as strip.h says;
// without a note it shows the scale alone.

typedef struct tuner_strip tuner_strip_t;

typedef struct {
	float tick_cents;          // between the scale's ticks
	strip_config_t strip;
} tuner_strip_config_t;

static const tuner_strip_config_t default_tuner_strip_config = {
	.tick_cents = 10.0f,
	.strip = default_strip_config,
};

// create a tuner's strip; NULL takes the default configuration
tuner_strip_t *tuner_strip_new(const tuner_strip_config_t *config);

// destroy it
void tuner_strip_free(tuner_strip_t **t);

// the configuration, and its replacement
const tuner_strip_config_t *tuner_strip_config(const tuner_strip_t *t);
void tuner_strip_set_config(tuner_strip_t *t, const tuner_strip_config_t *config);

// draw into the current viewport: the note, or NULL for none, valid now or
// held from before
void tuner_strip_render(tuner_strip_t *t, SDL_Renderer *renderer, const note_t *note, bool valid);

#endif /* tuner_strip_h */
