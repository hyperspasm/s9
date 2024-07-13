#include "fps.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

typedef struct fps {
	SDL_Renderer *renderer;
	TTF_Font *font;
	SDL_Color color;
	uint32_t interval_ms;
	
	uint32_t last_time;
	uint32_t frame_count;
	float fps_value;
	SDL_Texture *fps_texture;
	int width;
	int height;
	
} fps_t;

static void update_fps_texture(fps_t *fps) {
	char fps_text[32];
	snprintf(fps_text, sizeof(fps_text), "FPS: %.2f", fps->fps_value);

	SDL_Surface *surface = TTF_RenderUTF8_Blended(fps->font, fps_text, fps->color);
	CHECK_ERROR(surface != NULL, return, "failed to create FPS surface: %s", TTF_GetError());

	if (fps->fps_texture) {
		SDL_DestroyTexture(fps->fps_texture);
	}
	fps->fps_texture = SDL_CreateTextureFromSurface(fps->renderer, surface);
	CHECK_ERROR(fps->fps_texture != NULL, SDL_FreeSurface(surface); return, "failed to create fps texture: %s", SDL_GetError());

	fps->width = surface->w;
	fps->height = surface->h;
	SDL_FreeSurface(surface);
}

fps_t *fps_new(SDL_Renderer *renderer, TTF_Font *font, SDL_Color color, uint32_t interval_ms) {
	fps_t *fps = (fps_t *)malloc(sizeof(fps_t));
	CHECK_ERROR(fps != NULL, return NULL, "failed to allocate fps struct");
	memset(fps, 0, sizeof(*fps));

	fps->renderer = renderer;
	fps->interval_ms = interval_ms;
	fps->font = font;
	fps->color = color;
	fps->last_time = SDL_GetTicks();
	fps->frame_count = 0;
	fps->fps_value = 0.0f;
	fps->fps_texture = NULL;
	fps->width = 0;
	fps->height = 0;	
	
	return fps;
}

void fps_free(fps_t **fps) {
	if (fps && *fps) {
		if ((*fps)->fps_texture) {
			SDL_DestroyTexture((*fps)->fps_texture);
		}
		free(*fps);
		*fps = NULL;
	}
}

void fps_render(fps_t *fps) {
	uint32_t current_time = SDL_GetTicks();
	fps->frame_count++;
	if (current_time - fps->last_time >= fps->interval_ms) {
		fps->fps_value = fps->frame_count * 1000.0f / (current_time - fps->last_time);
		fps->frame_count = 0;
		fps->last_time = current_time;
		update_fps_texture(fps);
	}
	
	if (fps->fps_texture) {
		SDL_Rect dst_rect = { 0, 0, fps->width, fps->height };
		SDL_RenderCopy(fps->renderer, fps->fps_texture, NULL, &dst_rect);
	}
}
