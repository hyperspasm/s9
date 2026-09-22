#ifndef note_strip_h
#define note_strip_h

#include "note.h"
#include "strip.h"

#include <SDL3/SDL.h>
#include <stdbool.h>

// The note range: a scale of notes from a lowest to a highest, ticked at
// every semitone and named at every C, with a marker at a note and the
// frequency it was read from as text, and a dim marker at the lock the note
// was decided from. A strip, drawn as strip.h says; without a note it shows
// the scale alone, with the lock's marker if there is a lock.

typedef struct note_strip note_strip_t;

typedef struct {
	strip_config_t strip;
} note_strip_config_t;

static const note_strip_config_t default_note_strip_config = {
	.strip = default_strip_config,
};

// create a note range strip; NULL takes the default configuration
note_strip_t *note_strip_new(const note_strip_config_t *config);

// destroy it
void note_strip_free(note_strip_t **n);

// the configuration, and its replacement
const note_strip_config_t *note_strip_config(const note_strip_t *n);
void note_strip_set_config(note_strip_t *n, const note_strip_config_t *config);

// Draw into the current viewport: the range from low to high as note
// numbers, fractional as note_midi gives them; the note, or NULL for none,
// with the frequency in Hz it was read from, valid now or held from before;
// and the lock as a note, or NULL for none.
void note_strip_render(note_strip_t *n, SDL_Renderer *renderer, float low, float high, const note_t *note,
                       float frequency_hz, bool valid, const note_t *lock);

#endif /* note_strip_h */
