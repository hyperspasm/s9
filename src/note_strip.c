#include "note_strip.h"
#include "s9.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

struct note_strip {
	note_strip_config_t config;
	strip_t *strip;
};

const note_strip_config_t *note_strip_config(const note_strip_t *n) {
	return &n->config;
}

void note_strip_set_config(note_strip_t *n, const note_strip_config_t *config) {
	n->config = config ? *config : default_note_strip_config;
	strip_set_config(n->strip, &n->config.strip);
}

note_strip_t *note_strip_new(const note_strip_config_t *config) {
	note_strip_t *n = malloc(sizeof(note_strip_t));
	CHECK_ALLOC(n, return NULL);
	memset(n, 0, sizeof(*n));
	n->strip = strip_new(NULL);
	CHECK_ALLOC(n->strip, note_strip_free(&n); return NULL);
	note_strip_set_config(n, config);
	return n;
}

void note_strip_free(note_strip_t **n) {
	if (n && *n) {
		strip_free(&(*n)->strip);
		free(*n);
		*n = NULL;
	}
}

void note_strip_render(note_strip_t *n, SDL_Renderer *renderer, float low, float high, const note_t *note,
                       float frequency_hz, bool valid) {
	if (!(high > low)) return;
	strip_tick_t ticks[STRIP_TICKS_MAX];
	uint32_t count = 0;

	// a tick at every semitone in the range, the Cs named
	int first = (int)ceilf(low), last = (int)floorf(high);
	for (int k = first; k <= last && count < STRIP_TICKS_MAX; k++) {
		strip_tick_t *t = &ticks[count++];
		t->position = (k - low) / (high - low);
		t->major = ((k % 12) + 12) % 12 == 0;
		if (t->major) note_name(k, t->label, sizeof(t->label));
		else t->label[0] = '\0';
	}

	// the frequency as text, and the marker at the note
	char text[32] = "";
	float marker = -1.0f;
	if (note) {
		snprintf(text, sizeof(text), "%.1f Hz", frequency_hz);
		marker = (note_midi(note) - low) / (high - low);
		if (marker < 0.0f) marker = 0.0f;
	}
	strip_render(n->strip, renderer, text, ticks, count, marker, valid);
}
