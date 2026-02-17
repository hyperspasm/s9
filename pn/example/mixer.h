#ifndef mixer_h
#define mixer_h

#include <pn/pn.h>
#include <stdint.h>
#include <stdbool.h>

#define NUM_CHANNELS 8

typedef struct {
	float bass;
	float mid;
	float treble;
	bool enabled;
	pn_param_t nodes[4];
} eq_t;

typedef struct {
	float gain;
	float pan;
	uint32_t input; // 0 = mic, 1 = line
	eq_t eq;
	pn_param_t nodes[4];
	pn_param_t group;
	char name[8];
} channel_t;

typedef struct {
	float master_gain;
	bool master_mute;
	channel_t channels[NUM_CHANNELS];
	pn_param_t children[NUM_CHANNELS + 2];
	pn_param_t root;
} mixer_t;

void mixer_init(mixer_t *m);
pn_param_t *mixer_build_tree(mixer_t *m);

#endif /* mixer_h */
