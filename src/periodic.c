#include "periodic.h"
#include "s9.h"

#include <stdlib.h>

struct periodic {
	periodic_config_t config;
	int config_changed;              // applied at the next frame

	pitch_tracker_t *tracker;
	harmonics_t *harmonics;
};

periodic_config_t periodic_default_config(void) {
	periodic_config_t c = {
		.channel = S9_CHANNEL_ALL,
		.view = PERIODIC_VIEW_HARMONICS,
		.tracker = default_pitch_tracker_config,
		.harmonics = default_harmonics_config,
	};
	return c;
}

const periodic_config_t *periodic_config(const periodic_t *p) {
	return &p->config;
}

void periodic_set_config(periodic_t *p, const periodic_config_t *config) {
	p->config = config ? *config : periodic_default_config();
	if ((unsigned)p->config.view >= PERIODIC_VIEW_COUNT) p->config.view = PERIODIC_VIEW_HARMONICS;
	p->config_changed = 1;
}

// the tracker's configuration, on this component's channel
static pitch_tracker_config_t tracker_config(const periodic_t *p) {
	pitch_tracker_config_t c = p->config.tracker;
	c.channel = p->config.channel;
	return c;
}

// hand a changed configuration to the tracker and the views
static void apply_config(periodic_t *p) {
	if (!p->config_changed) return;
	p->config_changed = 0;
	pitch_tracker_config_t tc = tracker_config(p);
	pitch_tracker_set_config(p->tracker, &tc);
	harmonics_set_config(p->harmonics, &p->config.harmonics);
}

periodic_t *periodic_new(const periodic_config_t *config) {
	periodic_t *p = malloc(sizeof(periodic_t));
	CHECK_ALLOC(p, return NULL);
	memset(p, 0, sizeof(*p));

	periodic_set_config(p, config);
	p->config_changed = 0;

	pitch_tracker_config_t tc = tracker_config(p);
	p->tracker = pitch_tracker_new(&tc);
	CHECK_ALLOC(p->tracker, periodic_free(&p); return NULL);
	p->harmonics = harmonics_new(&p->config.harmonics, p->tracker);
	CHECK_ALLOC(p->harmonics, periodic_free(&p); return NULL);
	return p;
}

void periodic_free(periodic_t **p) {
	if (p && *p) {
		harmonics_free(&(*p)->harmonics);
		pitch_tracker_free(&(*p)->tracker);
		free(*p);
		*p = NULL;
	}
}

bool periodic_valid(const periodic_t *p) { return pitch_tracker_valid(p->tracker); }
float periodic_frequency(const periodic_t *p) { return pitch_tracker_frequency(p->tracker); }

const harmonic_reading_t *periodic_readings(const periodic_t *p, uint32_t *count) {
	return harmonics_readings(p->harmonics, count);
}

int periodic_thd(const periodic_t *p, float *thd) {
	return harmonics_get_thd(p->harmonics, thd);
}

// -- streaming

void periodic_prepare_to_stream(periodic_t *p, const s9_audio_stream_config_t *stream_config) {
	pitch_tracker_prepare_to_stream(p->tracker, stream_config);
	harmonics_prepare_to_stream(p->harmonics, stream_config);
}

void periodic_stop_streaming(periodic_t *p) {
	harmonics_stop_streaming(p->harmonics);
	pitch_tracker_stop_streaming(p->tracker);
}

void periodic_enqueue_audio_data(periodic_t *p, const void *data, uint32_t size) {
	pitch_tracker_enqueue_audio_data(p->tracker, data, size);
}

// -- the frame

void periodic_render(periodic_t *p, SDL_Renderer *renderer) {
	if (!pitch_tracker_streaming(p->tracker)) return;
	apply_config(p);
	pitch_tracker_update(p->tracker);
	switch (p->config.view) {
	case PERIODIC_VIEW_HARMONICS:
		harmonics_render(p->harmonics, renderer);
		break;
	default:
		break;
	}
}
