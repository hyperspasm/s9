#include "fps.h"
#include "draw_sdl.h"
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

fps_t *fps_new(SDL_Renderer *renderer, TTF_Font* (*load_font)(int point_size), SDL_Color color, uint32_t interval_ms) {
	fps_t *fps = (fps_t *)malloc(sizeof(fps_t));
	CHECK_ALLOC(fps, return NULL);
	memset(fps, 0, sizeof(*fps));
	
	fps->renderer = renderer;
	fps->interval_ms = interval_ms;
	fps->color = color;
	fps->last_time = SDL_GetTicks();
	fps->frame_count = 0;
	fps->fps_value = 0.0f;
	fps->fps_texture = NULL;
	fps->width = 0;
	fps->height = 0;	
	
	fps->font = load_font(16);
	CHECK_ERROR(fps->font != NULL,
				fps_free(&fps);
				return NULL,
				"Failed to load small font");
	
	return fps;
}

void fps_free(fps_t **fps) {
	if (fps && *fps) {
		if ((*fps)->fps_texture) {
			SDL_DestroyTexture((*fps)->fps_texture);
		}
		
		if((*fps)->font) {
			TTF_CloseFont((*fps)->font);
			(*fps)->font = NULL;
		}
		
		free(*fps);
		*fps = NULL;
	}
}

static void update_fps_texture(fps_t *fps) {
	char fps_text[32];
	snprintf(fps_text, sizeof(fps_text), "%.2f", fps->fps_value);	
	draw_sdl_render_text(fps->renderer, &fps->fps_texture, fps->font, &fps->color, fps_text, &fps->width, &fps->height);
}

void fps_render(fps_t *fps, SDL_Renderer *renderer) {
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
