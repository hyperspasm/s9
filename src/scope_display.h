#ifndef scope_display_h
#define scope_display_h

#include "s9_colors.h"

#include <SDL2/SDL.h>

typedef struct scope_display scope_display_t;

typedef struct {
	SDL_Color trace_color;
	uint8_t persistence;  // 0-255, higher = longer phosphor persistence
} scope_display_config_t;

static const scope_display_config_t default_scope_display_config = {
	.trace_color = S9_WHITE,
	.persistence = 200,
};

// create scope display
scope_display_t *scope_display_new(const scope_display_config_t *config);

// destroy scope display
void scope_display_free(scope_display_t **display);

// update config
void scope_display_set_config(scope_display_t *display, const scope_display_config_t *config);

// set label text (NULL to clear)
void scope_display_set_label_bottom(scope_display_t *display, const char *label);
void scope_display_set_label_top(scope_display_t *display, const char *label);

// invalidate cached rendering state — forces reinit on next prepare
void scope_display_invalidate(scope_display_t *display);

// prepare for rendering — checks viewport changes, recreates textures if needed
// returns: -1 on error, 0 if no change, 1 if reinitialized
int scope_display_prepare(scope_display_t *display, SDL_Renderer *renderer);

// get the frame rect (valid after prepare)
const SDL_Rect *scope_display_frame_rect(scope_display_t *display);

// begin frame — toggles blur buffers, sets render target, clips to frame rect, clears
void scope_display_begin_frame(scope_display_t *display, SDL_Renderer *renderer);

// end frame — persistence blend, MIN clip, blit to screen, render graticule
void scope_display_end_frame(scope_display_t *display, SDL_Renderer *renderer);

#endif /* scope_display_h */
