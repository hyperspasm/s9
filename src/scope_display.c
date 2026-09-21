#include "scope_display.h"

#include "s9.h"
#include "draw_sdl.h"
#include "graticule.h"
#include <stdlib.h>

typedef struct scope_display {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	SDL_Rect frame_rect;
	graticule_t *graticule;

	SDL_Texture *blur_buffer[2];
	uint32_t blur_buffer_index;
	SDL_Texture *clip_texture;
	SDL_BlendMode clip_blend_mode;

	scope_display_config_t config;
} scope_display_t;

static void free_textures(scope_display_t *d) {
	for(int i = 0; i < 2; i++) {
		if(d->blur_buffer[i]) {
			SDL_DestroyTexture(d->blur_buffer[i]);
			d->blur_buffer[i] = NULL;
		}
	}

	if(d->clip_texture) {
		SDL_DestroyTexture(d->clip_texture);
		d->clip_texture = NULL;
	}
}

void scope_display_set_config(scope_display_t *d, const scope_display_config_t *config) {
	if(config) {
		d->config = *config;
	} else {
		d->config = default_scope_display_config;
	}
	// force texture recreation on next prepare (clip texture color may have changed)
	d->renderer = NULL;
}

scope_display_t *scope_display_new(const scope_display_config_t *config) {
	scope_display_t *d = (scope_display_t *) malloc(sizeof(scope_display_t));
	CHECK_ALLOC(d, return NULL);
	memset(d, 0, sizeof(*d));

	scope_display_set_config(d, config);

	d->graticule = graticule_new(NULL);
	CHECK_ALLOC(d->graticule, free(d); return NULL);

	d->clip_blend_mode = SDL_ComposeCustomBlendMode(
		SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MINIMUM,
		SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MINIMUM
	);

	return d;
}

void scope_display_free(scope_display_t **d) {
	if(d && *d) {
		free_textures(*d);
		graticule_free(&(*d)->graticule);
		free(*d);
		*d = NULL;
	}
}

void scope_display_set_label_bottom(scope_display_t *d, const char *label) {
	graticule_set_label_bottom(d->graticule, label);
}

void scope_display_set_label_top(scope_display_t *d, const char *label) {
	graticule_set_label_top(d->graticule, label);
}

void scope_display_invalidate(scope_display_t *d) {
	d->renderer = NULL;
}

int scope_display_prepare(scope_display_t *d, SDL_Renderer *renderer) {
	SDL_Rect viewport;
	SDL_RenderGetViewport(renderer, &viewport);

	if(d->renderer == renderer && SDL_RectEquals(&d->viewport, &viewport)) {
		return 0;
	}

	d->renderer = renderer;
	d->viewport = viewport;

	// check if renderer supports MIN blend operation
	SDL_RendererInfo info;
	int min_blend_supported = 0;
	if(SDL_GetRendererInfo(renderer, &info) == 0) {
		const char *name = info.name;
		if(strstr(name, "direct3d") != NULL || strstr(name, "metal") != NULL) {
			min_blend_supported = 1;
		}
	}
	if(!min_blend_supported) {
		d->clip_blend_mode = SDL_BLENDMODE_INVALID;
	}

	free_textures(d);

	d->blur_buffer[0] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, viewport.w, viewport.h);
	CHECK_ALLOC(d->blur_buffer[0], return -1);
	SDL_SetRenderTarget(renderer, d->blur_buffer[0]);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);

	d->blur_buffer[1] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, viewport.w, viewport.h);
	CHECK_ALLOC(d->blur_buffer[1], return -1);
	SDL_SetRenderTarget(renderer, d->blur_buffer[1]);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);

	d->blur_buffer_index = 0;

	if(d->clip_blend_mode != SDL_BLENDMODE_INVALID) {
		d->clip_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, viewport.w, viewport.h);
		CHECK_ALLOC(d->clip_texture, return -1);
		SDL_SetRenderTarget(renderer, d->clip_texture);
		SDL_SetRenderDrawColor(renderer, d->config.trace_color.r, d->config.trace_color.g, d->config.trace_color.b, 255);
		SDL_RenderClear(renderer);
	}

	d->frame_rect = (SDL_Rect){ .x = 10, .y = 10, .w = viewport.w - 20, .h = viewport.h - 20 };

	return 1;
}

const SDL_Rect *scope_display_frame_rect(scope_display_t *d) {
	return &d->frame_rect;
}

void scope_display_begin_frame(scope_display_t *d, SDL_Renderer *renderer) {
	d->blur_buffer_index = d->blur_buffer_index == 0;
	SDL_Texture *cur = d->blur_buffer[d->blur_buffer_index];

	SDL_SetRenderTarget(renderer, cur);
	SDL_RenderSetClipRect(renderer, &d->frame_rect);
	draw_sdl_clear_viewport(renderer, NULL);
}

void scope_display_end_frame(scope_display_t *d, SDL_Renderer *renderer) {
	SDL_Texture *cur = d->blur_buffer[d->blur_buffer_index];
	SDL_Texture *prv = d->blur_buffer[!d->blur_buffer_index];

	// clear clip rect before blending
	SDL_RenderSetClipRect(renderer, NULL);

	// blend in history for persistence effect
	SDL_SetTextureBlendMode(prv, SDL_BLENDMODE_ADD);
	SDL_SetTextureAlphaMod(prv, d->config.persistence);
	SDL_RenderCopy(renderer, prv, NULL, NULL);

	// clip to trace color using MIN blend (prevents hue shift)
	if(d->clip_texture && d->clip_blend_mode != SDL_BLENDMODE_INVALID) {
		SDL_SetTextureBlendMode(d->clip_texture, d->clip_blend_mode);
		SDL_RenderCopy(renderer, d->clip_texture, NULL, NULL);
	}

	// blit current buffer to screen
	SDL_SetRenderTarget(renderer, NULL);
	SDL_SetTextureBlendMode(cur, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, cur, NULL, NULL);

	// render graticule on top
	graticule_render(d->graticule, renderer, &d->frame_rect);
}
