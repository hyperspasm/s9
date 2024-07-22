#include "scratchpad.h"
#include "s9.h"
#include "draw_sdl.h"
#include "fresp_graph.h"


typedef struct scratchpad {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	
	TTF_Font* (*load_font)(int point_size);
	sdl_path_t *wave_path;
	fresp_graph_t *frg;
	
} scratchpad_t;

scratchpad_t *scratchpad_new(TTF_Font* (*load_font)(int point_size)) {
	scratchpad_t *sp = (scratchpad_t *) malloc(sizeof(scratchpad_t));
	CHECK_ALLOC(sp, return NULL);
	memset(sp, 0, sizeof(*sp));
	
	sp->load_font = load_font;
	sp->frg = fresp_graph_new(NULL, sp->load_font);
	
	return sp;
}

void scratchpad_free(scratchpad_t **sp) {
	if (sp && *sp) {
		
		if((*sp)->wave_path) {
			sdl_path_free(&((*sp)->wave_path));
		}
		
		if((*sp)->frg) {
			fresp_graph_free(&((*sp)->frg));
		}
		
		free(*sp);
		*sp = NULL;
	}
}

static inline float *compute_wave(uint32_t n_points, float amplitude, float offset) {
	float *points = malloc(sizeof(float) * n_points);
	
	float frequency = 1.f/n_points;
	
	for(int i = 0; i < n_points; i++) {
		float base_wave = sinf(frequency * (float)i * 2.0f * M_PI);
		float second_harmonic = 0.1f * sinf(2.0f * frequency * (float)i * 2.0f * M_PI);
		float fourth_harmonic = 0.2f * sinf(4.0f * frequency * (float)i * 2.0f * M_PI);
		float fifth_harmonic = 0.1f * sinf(5.0f * frequency * (float)i * 2.0f * M_PI);
		float wave =  (base_wave + second_harmonic + fourth_harmonic + fifth_harmonic) / 1.4f; // normalizing by 1.4 to maintain the amplitude within bounds				
		wave = wave * 0.5f + 0.5f; // scale 0.0 - 1.0 
		
		points[i] = offset + amplitude * wave;
	}
	
	return points;
}

static inline void prepare_wave(scratchpad_t *sp) {
	
	if(sp->wave_path) {
		sdl_path_free(&sp->wave_path);
	}
	
	int inset = 10;
	float width = sp->viewport.w - 2 * inset;
	float height = sp->viewport.h - 2 * inset;
	
	CHECK_ERROR(width > 10, return, "scratchpad viewport too narrow");
	
	SDL_FPoint *wave_points = (SDL_FPoint*)malloc(sizeof(SDL_FPoint) * (size_t)width);
	CHECK_ALLOC(wave_points, return);
	
	float *wave = compute_wave(width, height, inset);
	CHECK_ERROR(wave != NULL, return, "failed to compute wave");
	for(int i = 0; i < width; i++) {
		wave_points[i].x = inset + i;
		wave_points[i].y = wave[i];
	}
	free(wave);
	
	SDL_Color color = { .r = 255, .g = 255, .b = 0, .a = 255 };
	sp->wave_path = sdl_path_new(&color, 10, 1, wave_points, width);
	CHECK_ERROR(wave != NULL, return, "failed to create wave path");
	
	free(wave_points);
}

static inline void prepare_to_draw(scratchpad_t *sp, SDL_Renderer *renderer) {
	SDL_Rect viewport;	
	SDL_RenderGetViewport(renderer, &viewport);
	
	if(sp->renderer != renderer || !SDL_RectEquals(&sp->viewport, &viewport)) {
		sp->renderer = renderer;
		sp->viewport = viewport;		
		S9_LOG_DEBUG("scratchpad viewport { %d, %d, %d, %d }", viewport.x, viewport.y, viewport.w, viewport.h);
		
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
	
	if(sp->frg) {
		fresp_graph_render(sp->frg, renderer);
	}	
}
