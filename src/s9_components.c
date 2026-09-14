#include "s9_components.h"
#include "s9.h"
#include "fps.h"
#include "vu.h"
#include "spectrogram.h"
#include "oscilloscope.h"
#include "lissajous.h"
#include "periodic.h"

#include <stdlib.h>

struct s9_components {
	s9_component_t components[COMP_COUNT];
};

s9_components_t *s9_components_new(void) {
	s9_components_t *comps = malloc(sizeof(s9_components_t));
	CHECK_ALLOC(comps, return NULL);
	memset(comps, 0, sizeof(*comps));

	// fps
	fps_t *fps = fps_new((SDL_Color){255, 255, 255, 255}, 1000);
	CHECK_ERROR(fps != NULL, s9_components_free(&comps); return NULL, "Failed to create fps instance");
	comps->components[COMP_FPS] = (s9_component_t){
		.instance = fps,
		.destroy = (void (*)(void **))fps_free,
		.render = (void (*)(void *, SDL_Renderer *))fps_render,
	};

	// vu
	vu_t *vu = vu_new(NULL);
	CHECK_ERROR(vu != NULL, s9_components_free(&comps); return NULL, "Failed to create vu instance");
	comps->components[COMP_VU] = (s9_component_t){
		.instance = vu,
		.destroy = (void (*)(void **))vu_free,
		.render = (void (*)(void *, SDL_Renderer *))vu_render,
		.enqueue_audio = (void (*)(void *, uint8_t *, int))vu_enqueue_audio_data,
		.prepare_to_stream = (void (*)(void *, const s9_audio_stream_config_t *))vu_prepare_to_stream,
		.stop_streaming = (void (*)(void *))vu_stop_streaming,
	};

	// spectrogram
	spectrogram_t *sg = spectrogram_new(NULL);
	CHECK_ERROR(sg != NULL, s9_components_free(&comps); return NULL, "Failed to create spectrogram instance");
	comps->components[COMP_SPECTROGRAM] = (s9_component_t){
		.instance = sg,
		.destroy = (void (*)(void **))spectrogram_free,
		.render = (void (*)(void *, SDL_Renderer *))spectrogram_render,
		.enqueue_audio = (void (*)(void *, uint8_t *, int))spectrogram_enqueue_audio_data,
		.prepare_to_stream = (void (*)(void *, const s9_audio_stream_config_t *))spectrogram_prepare_to_stream,
		.stop_streaming = (void (*)(void *))spectrogram_stop_streaming,
	};

	// oscilloscope
	oscilloscope_t *osc = oscilloscope_new(NULL);
	CHECK_ERROR(osc != NULL, s9_components_free(&comps); return NULL, "Failed to create oscilloscope instance");
	comps->components[COMP_OSCILLOSCOPE] = (s9_component_t){
		.instance = osc,
		.destroy = (void (*)(void **))oscilloscope_free,
		.render = (void (*)(void *, SDL_Renderer *))oscilloscope_render,
		.enqueue_audio = (void (*)(void *, uint8_t *, int))oscilloscope_enqueue_audio_data,
		.prepare_to_stream = (void (*)(void *, const s9_audio_stream_config_t *))oscilloscope_prepare_to_stream,
		.stop_streaming = (void (*)(void *))oscilloscope_stop_streaming,
	};

	// lissajous
	lissajous_t *lissajous = lissajous_new(NULL);
	CHECK_ERROR(lissajous != NULL, s9_components_free(&comps); return NULL, "Failed to create lissajous instance");
	comps->components[COMP_LISSAJOUS] = (s9_component_t){
		.instance = lissajous,
		.destroy = (void (*)(void **))lissajous_free,
		.render = (void (*)(void *, SDL_Renderer *))lissajous_render,
		.enqueue_audio = (void (*)(void *, uint8_t *, int))lissajous_enqueue_audio_data,
		.prepare_to_stream = (void (*)(void *, const s9_audio_stream_config_t *))lissajous_prepare_to_stream,
		.stop_streaming = (void (*)(void *))lissajous_stop_streaming,
	};

	// periodic
	periodic_t *periodic = periodic_new(NULL);
	CHECK_ERROR(periodic != NULL, s9_components_free(&comps); return NULL, "Failed to create periodic instance");
	comps->components[COMP_PERIODIC] = (s9_component_t){
		.instance = periodic,
		.destroy = (void (*)(void **))periodic_free,
		.render = (void (*)(void *, SDL_Renderer *))periodic_render,
		.enqueue_audio = (void (*)(void *, uint8_t *, int))periodic_enqueue_audio_data,
		.prepare_to_stream = (void (*)(void *, const s9_audio_stream_config_t *))periodic_prepare_to_stream,
		.stop_streaming = (void (*)(void *))periodic_stop_streaming,
	};

	return comps;
}

void s9_components_free(s9_components_t **comps) {
	if (comps && *comps) {
		for (int i = 0; i < COMP_COUNT; i++) {
			s9_component_t *c = &(*comps)->components[i];
			if (c->instance && c->destroy) {
				c->destroy(&c->instance);
			}
		}
		free(*comps);
		*comps = NULL;
	}
}

s9_component_t *s9_components_get(s9_components_t *comps, s9_component_id_t id) {
	if (!comps || id < 0 || id >= COMP_COUNT) return NULL;
	return &comps->components[id];
}

// -- modes

static const s9_mode_slot_t mode_full[] = {
	{ COMP_FPS,           { .margin_left = 10,  .margin_top = 10,  .fixed_width = 50, .fixed_height = 40  } },
	{ COMP_VU,            { .margin_left = 10,  .margin_top = 40,  .margin_bottom = 10, .fixed_width = 50 } },
	{ COMP_SPECTROGRAM,   { .margin_left = 60,  .fixed_width = 860 } },
	{ COMP_OSCILLOSCOPE,  { .margin_left = 920, .fixed_height = 360 } },
	{ COMP_LISSAJOUS,     { .margin_left = 920, .margin_top = 360, .fixed_height = 360 } },
	{ COMP_NONE, {0} },
};

static const s9_mode_slot_t mode_scope[] = {
	{ COMP_FPS,           { .margin_left = 10,  .margin_top = 10,  .fixed_width = 50, .fixed_height = 40  } },
	{ COMP_VU,            { .margin_left = 10,  .margin_top = 40,  .margin_bottom = 10, .fixed_width = 50 } },
	{ COMP_OSCILLOSCOPE,  { .margin_left = 60 } },
	{ COMP_NONE, {0} },
};

static const s9_mode_slot_t mode_spectrum[] = {
	{ COMP_FPS,           { .margin_left = 10,  .margin_top = 10,  .fixed_width = 50, .fixed_height = 40  } },
	{ COMP_VU,            { .margin_left = 10,  .margin_top = 40,  .margin_bottom = 10, .fixed_width = 50 } },
	{ COMP_SPECTROGRAM,   { .margin_left = 60 } },
	{ COMP_NONE, {0} },
};

static const s9_mode_slot_t mode_periodic[] = {
	{ COMP_FPS,           { .margin_left = 10,  .margin_top = 10,  .fixed_width = 50, .fixed_height = 40  } },
	{ COMP_VU,            { .margin_left = 10,  .margin_top = 40,  .margin_bottom = 10, .fixed_width = 50 } },
	{ COMP_PERIODIC,      { .margin_left = 60 } },
	{ COMP_NONE, {0} },
};

static const s9_mode_slot_t *const mode_slots[S9_MODE_COUNT] = {
	[S9_MODE_FULL]      = mode_full,
	[S9_MODE_SCOPE]     = mode_scope,
	[S9_MODE_SPECTRUM]  = mode_spectrum,
	[S9_MODE_PERIODIC]  = mode_periodic,
};

const s9_mode_slot_t *s9_mode_slots(s9_mode_id_t mode) {
	if ((unsigned)mode >= S9_MODE_COUNT) return NULL;
	return mode_slots[mode];
}
