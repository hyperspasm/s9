#ifndef s9_components_h
#define s9_components_h

#include "s9.h"
#include "s9_dsp.h"
#include "s9_modes.h"

#include <SDL3/SDL_render.h>

// component identification
typedef enum {
	COMP_NONE = -1,
	COMP_FPS,
	COMP_VU,
	COMP_SPECTRUM,
	COMP_OSCILLOSCOPE,
	COMP_LISSAJOUS,
	COMP_PERIODIC,
	COMP_SPECTROGRAM,
	COMP_COUNT,
} s9_component_id_t;

// component vtable
typedef struct s9_component {
	void *instance;

	void (*destroy)(void **instance);
	void (*render)(void *instance, SDL_Renderer *renderer);
	void (*enqueue_audio)(void *instance, uint8_t *stream, int len);
	void (*prepare_to_stream)(void *instance, const s9_audio_stream_config_t *config);
	void (*stop_streaming)(void *instance);
} s9_component_t;

// layout definition
typedef struct s9_layout {
	uint32_t margin_left, margin_top, margin_right, margin_bottom;
	uint32_t fixed_width;   // >0 ignores margin_right; 0 fills remaining
	uint32_t fixed_height;  // >0 ignores margin_bottom; 0 fills remaining
} s9_layout_t;

// component slot (binds component to layout)
typedef struct s9_component_slot {
	s9_component_t *component;
	s9_layout_t layout;
} s9_component_slot_t;

// opaque component manager
typedef struct s9_components s9_components_t;

s9_components_t *s9_components_new(void);
void s9_components_free(s9_components_t **comps);
s9_component_t *s9_components_get(s9_components_t *comps, s9_component_id_t id);

// mode descriptor — sentinel-terminated array (id == COMP_NONE)
typedef struct s9_mode_slot {
	s9_component_id_t id;
	s9_layout_t layout;
} s9_mode_slot_t;

// the components and layout of a mode, or NULL for an invalid id
const s9_mode_slot_t *s9_mode_slots(s9_mode_id_t mode);

#endif /* s9_components_h */
