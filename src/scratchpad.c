#include "scratchpad.h"
#include "s9.h"
#include "draw_sdl.h"


typedef struct scratchpad {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	
	sdl_path_t *wave_path;
	
} scratchpad_t;

scratchpad_t *scratchpad_new(void) {
	scratchpad_t *sp = (scratchpad_t *) malloc(sizeof(scratchpad_t));
	CHECK_ALLOC(sp, return NULL);
	memset(sp, 0, sizeof(*sp));
	
	return sp;
}

void scratchpad_free(scratchpad_t **sp) {
	if (sp && *sp) {
		
		if((*sp)->wave_path) {
			sdl_path_free(&((*sp)->wave_path));
		}
		
		free(*sp);
		*sp = NULL;
	}
}

static inline void prepare_wave(scratchpad_t *sp) {
	
	if(sp->wave_path) {
		sdl_path_free(&sp->wave_path);
	}
	
	int inset = 10;
	float width = sp->viewport.w - 2 * inset;
	float height = sp->viewport.h - 2 * inset;
	float frequency = 1.0f / width;
	float amplitude = height / 2.0f;
	float offset_y = inset + amplitude;
	
	CHECK_ERROR(width > 10, return, "scratchpad viewport too narrow");
	
	SDL_FPoint *wave_points = (SDL_FPoint*)malloc(sizeof(SDL_FPoint) * (size_t)width);
	CHECK_ALLOC(wave_points, return);
	int n_wave_points = (int)width;
	
	for(int i = 0; i < n_wave_points; i++) {
		float x = inset + (float)i;
		float base_wave = sinf(frequency * (float)i * 2.0f * M_PI);
		float second_harmonic = 0.1f * sinf(2.0f * frequency * (float)i * 2.0f * M_PI);
		float fourth_harmonic = 0.2f * sinf(4.0f * frequency * (float)i * 2.0f * M_PI);
		float fifth_harmonic = 0.1f * sinf(5.0f * frequency * (float)i * 2.0f * M_PI);
		float y = offset_y + amplitude * (base_wave + second_harmonic + fourth_harmonic + fifth_harmonic) / 1.4f; // Normalizing by 1.4 to maintain the amplitude within bounds
		wave_points[i].x = x;
		wave_points[i].y = y;
	}
	
	SDL_Color color = { .r = 255, .g = 255, .b = 0, .a = 255 };
	sp->wave_path = sdl_path_new(&color, 10, 1, wave_points, n_wave_points);
	
	free(wave_points);
}

static inline void prepare_to_draw(scratchpad_t *sp, SDL_Renderer *renderer) {
	SDL_Rect viewport;	
	SDL_RenderGetViewport(renderer, &viewport);
	
	if(sp->renderer != renderer || !SDL_RectEquals(&sp->viewport, &viewport)) {
		sp->renderer = renderer;
		sp->viewport = viewport;		
		s9_log(LOG_LEVEL_DEBUG, "scratchpad viewport { %d, %d, %d, %d }", viewport.x, viewport.y, viewport.w, viewport.h);
		
		prepare_wave(sp);
	}
}

static inline void draw_wave(scratchpad_t *sp, SDL_Renderer *renderer) {
	if(sp->wave_path) {
		sdl_path_render(sp->wave_path, renderer);
	}
}

void scratchpad_render(scratchpad_t *sp, SDL_Renderer *renderer) {
	prepare_to_draw(sp, renderer);
	draw_wave(sp, renderer);
}
