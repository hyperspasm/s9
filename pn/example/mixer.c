#include "mixer.h"
#include <stdio.h>
#include <string.h>

#ifndef PN_SCHEMA_ONLY

// --- EQ getters/setters ---

static void  set_eq_bass(void *ctx, float val)    { eq_t *eq = ctx; printf("eq bass = %.1f dB\n", val); eq->bass = val; }
static float get_eq_bass(void *ctx)               { return ((eq_t *)ctx)->bass; }

static void  set_eq_mid(void *ctx, float val)     { eq_t *eq = ctx; printf("eq mid = %.1f dB\n", val); eq->mid = val; }
static float get_eq_mid(void *ctx)                { return ((eq_t *)ctx)->mid; }

static void  set_eq_treble(void *ctx, float val)  { eq_t *eq = ctx; printf("eq treble = %.1f dB\n", val); eq->treble = val; }
static float get_eq_treble(void *ctx)             { return ((eq_t *)ctx)->treble; }

static void set_eq_enabled(void *ctx, bool val)   { eq_t *eq = ctx; printf("eq enabled = %d\n", val); eq->enabled = val; }
static bool get_eq_enabled(void *ctx)             { return ((eq_t *)ctx)->enabled; }

// --- Channel getters/setters ---

static void  set_gain(void *ctx, float val)       { channel_t *ch = ctx; printf("gain = %.1f dB\n", val); ch->gain = val; }
static float get_gain(void *ctx)                  { return ((channel_t *)ctx)->gain; }

static void  set_pan(void *ctx, float val)        { channel_t *ch = ctx; printf("pan = %.2f\n", val); ch->pan = val; }
static float get_pan(void *ctx)                   { return ((channel_t *)ctx)->pan; }

static void     set_input(void *ctx, uint32_t val)  { channel_t *ch = ctx; printf("input = %u\n", val); ch->input = val; }
static uint32_t get_input(void *ctx)                { return ((channel_t *)ctx)->input; }

// --- Master getters/setters ---

static void  set_master_gain(void *ctx, float val) { mixer_t *m = ctx; printf("master gain = %.1f dB\n", val); m->master_gain = val; }
static float get_master_gain(void *ctx)            { return ((mixer_t *)ctx)->master_gain; }

static void set_master_mute(void *ctx, bool val)   { mixer_t *m = ctx; printf("master mute = %d\n", val); m->master_mute = val; }
static bool get_master_mute(void *ctx)             { return ((mixer_t *)ctx)->master_mute; }

#endif /* PN_SCHEMA_ONLY */

// --- Init ---

void mixer_init(mixer_t *m) {
	memset(m, 0, sizeof(*m));
}

// --- Tree ---

static const char *const input_labels[] = { "mic", "line" };

pn_param_t *mixer_build_tree(mixer_t *m) {
	for (int i = 0; i < NUM_CHANNELS; i++) {
		channel_t *ch = &m->channels[i];
		eq_t *eq = &ch->eq;

		// EQ nodes
		uint32_t e = 0;
		eq->nodes[e++] = pn_bool("enabled", eq, PN_FN(set_eq_enabled), PN_FN(get_eq_enabled));
		eq->nodes[e++] = pn_unit(pn_float("bass",   eq, PN_FN(set_eq_bass),    PN_FN(get_eq_bass),   -12.0f, 12.0f, 0.1f), "dB");
		eq->nodes[e++] = pn_unit(pn_float("mid",    eq, PN_FN(set_eq_mid),     PN_FN(get_eq_mid),    -12.0f, 12.0f, 0.1f), "dB");
		eq->nodes[e++] = pn_unit(pn_float("treble", eq, PN_FN(set_eq_treble),  PN_FN(get_eq_treble), -12.0f, 12.0f, 0.1f), "dB");

		// Channel nodes
		uint32_t c = 0;
		ch->nodes[c++] = pn_unit(pn_float("gain",  ch, PN_FN(set_gain),  PN_FN(get_gain),  -72.0f, 6.0f, 0.1f), "dB");
		ch->nodes[c++] = pn_float("pan",   ch, PN_FN(set_pan),   PN_FN(get_pan),   -1.0f,  1.0f, 0.01f);
		ch->nodes[c++] = pn_enum("input",  ch, PN_FN(set_input), PN_FN(get_input), input_labels, 2);
		ch->nodes[c++] = pn_group("eq", eq->nodes, e);

		// Channel group
		snprintf(ch->name, sizeof(ch->name), "ch%d", i + 1);
		ch->group = pn_group(ch->name, ch->nodes, c);
	}

	// Mixer root children
	uint32_t n = 0;
	m->children[n++] = pn_unit(pn_float("master_gain", m, PN_FN(set_master_gain), PN_FN(get_master_gain), -72.0f, 6.0f, 0.1f), "dB");
	m->children[n++] = pn_bool("master_mute",  m, PN_FN(set_master_mute), PN_FN(get_master_mute));
	for (int i = 0; i < NUM_CHANNELS; i++)
		m->children[n++] = m->channels[i].group;

	m->root = pn_group("mixer", m->children, n);
	return &m->root;
}
