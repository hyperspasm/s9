#include "tuner_strip.h"
#include "s9.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define SPAN_CENTS 50.0f        // the scale reaches this far either side of the note: to the next one

struct tuner_strip {
	tuner_strip_config_t config;
	strip_t *strip;
};

const tuner_strip_config_t *tuner_strip_config(const tuner_strip_t *t) {
	return &t->config;
}

void tuner_strip_set_config(tuner_strip_t *t, const tuner_strip_config_t *config) {
	t->config = config ? *config : default_tuner_strip_config;
	if (!(t->config.tick_cents > 0.0f)) t->config.tick_cents = default_tuner_strip_config.tick_cents;
	strip_set_config(t->strip, &t->config.strip);
}

tuner_strip_t *tuner_strip_new(const tuner_strip_config_t *config) {
	tuner_strip_t *t = malloc(sizeof(tuner_strip_t));
	CHECK_ALLOC(t, return NULL);
	memset(t, 0, sizeof(*t));
	t->strip = strip_new(NULL);
	CHECK_ALLOC(t->strip, tuner_strip_free(&t); return NULL);
	tuner_strip_set_config(t, config);
	return t;
}

void tuner_strip_free(tuner_strip_t **t) {
	if (t && *t) {
		strip_free(&(*t)->strip);
		free(*t);
		*t = NULL;
	}
}

// where cents fall along the scale
static float position(float cents) {
	return (cents + SPAN_CENTS) / (2.0f * SPAN_CENTS);
}

void tuner_strip_render(tuner_strip_t *t, SDL_Renderer *renderer, const note_t *note, bool valid) {
	strip_tick_t ticks[STRIP_TICKS_MAX];
	uint32_t count = 0;

	// a tick every tick_cents, except where the numbered ones stand
	float step = t->config.tick_cents;
	int n = (int)lroundf(2.0f * SPAN_CENTS / step);
	for (int i = 0; i <= n && count < STRIP_TICKS_MAX - 3; i++) {
		float cents = -SPAN_CENTS + i * step;
		if (cents > SPAN_CENTS || fabsf(cents) < step / 2.0f || fabsf(fabsf(cents) - SPAN_CENTS) < step / 2.0f) continue;
		ticks[count++] = (strip_tick_t){ .position = position(cents), .major = false, .label = "" };
	}
	// the ends and the note, numbered
	for (int i = -1; i <= 1; i++) {
		strip_tick_t *k = &ticks[count++];
		k->position = position(i * SPAN_CENTS);
		k->major = true;
		if (i == 0) snprintf(k->label, sizeof(k->label), "0");
		else snprintf(k->label, sizeof(k->label), "%+d", (int)(i * SPAN_CENTS));
	}

	// the note and its cents as text, and the marker at the cents
	char text[32] = "";
	float marker = -1.0f;
	if (note) {
		char name[NOTE_NAME_MAX];
		note_name(note->number, name, sizeof(name));
		int cents = (int)lrintf(note->cents);
		if (cents == 0) snprintf(text, sizeof(text), "%s 0", name);
		else snprintf(text, sizeof(text), "%s %+d", name, cents);
		marker = position(note->cents);
	}
	strip_render(t->strip, renderer, text, ticks, count, marker, valid);
}
