#include "fps.h"
#include "s9_font.h"
#include "draw_sdl.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

typedef struct fps {
	TTF_Font *font;
	float scale;            // the renderer's, that the font was opened for
	SDL_Color color;
	uint32_t interval_ms;
	
	uint64_t last_time;
	uint32_t frame_count;
	float fps_value;
	SDL_Texture *fps_texture;
	int width;
	int height;
	
} fps_t;

fps_t *fps_new(SDL_Color color, uint32_t interval_ms) {
	fps_t *fps = (fps_t *)malloc(sizeof(fps_t));
	CHECK_ALLOC(fps, return NULL);
	memset(fps, 0, sizeof(*fps));

	fps->interval_ms = interval_ms;
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
		
		if((*fps)->font) {
			TTF_CloseFont((*fps)->font);
			(*fps)->font = NULL;
		}
		
		free(*fps);
		*fps = NULL;
	}
}

static void update_fps_texture(fps_t *fps, SDL_Renderer *renderer) {
	char fps_text[32];
	snprintf(fps_text, sizeof(fps_text), "%.2f", fps->fps_value);
	draw_sdl_render_text(renderer, &fps->fps_texture, fps->font, &fps->color, fps_text, &fps->width, &fps->height);
}

void fps_render(fps_t *fps, SDL_Renderer *renderer) {
	// the font at the renderer's scale, opened at the first frame and again when the scale changes
	float scale = draw_sdl_scale(renderer);
	if (!fps->font || scale != fps->scale) {
		if (fps->font) TTF_CloseFont(fps->font);
		fps->font = s9_open_font(16.0f * scale);
		fps->scale = scale;
		CHECK_ERROR(fps->font != NULL, return, "Failed to load small font");
		update_fps_texture(fps, renderer);
	}

	uint64_t current_time = SDL_GetTicks();
	fps->frame_count++;
	if (current_time - fps->last_time >= fps->interval_ms) {
		fps->fps_value = fps->frame_count * 1000.0f / (current_time - fps->last_time);
		fps->frame_count = 0;
		fps->last_time = current_time;
		update_fps_texture(fps, renderer);
	}

	if (fps->fps_texture) {
		SDL_FRect dst_rect = { 0.0f, 0.0f, (float)fps->width, (float)fps->height };
		SDL_RenderTexture(renderer, fps->fps_texture, NULL, &dst_rect);
	}
}
