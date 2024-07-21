#include "scratchpad.h"
#include "s9.h"
#include "draw_sdl.h"
#include "fresp_graph.h"


typedef struct scratchpad {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	
	sdl_path_t *wave_path;
	fresp_graph_t *frg;
	
} scratchpad_t;

scratchpad_t *scratchpad_new(void) {
	scratchpad_t *sp = (scratchpad_t *) malloc(sizeof(scratchpad_t));
	CHECK_ALLOC(sp, return NULL);
	memset(sp, 0, sizeof(*sp));
	
	sp->frg = fresp_graph_new(NULL);
	
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
		S9_LOG_DEBUG("scratchpad viewport { %d, %d, %d, %d }", viewport.x, viewport.y, viewport.w, viewport.h);
		
		prepare_wave(sp);
	}
}

static inline void draw_wave(scratchpad_t *sp, SDL_Renderer *renderer) {
	if(sp->wave_path) {
		sdl_path_render(sp->wave_path, renderer);
	}
}

static inline void draw_graph(scratchpad_t *sp, SDL_Renderer *renderer) {
	SDL_Color color1 = { .r = 255,.g = 255, .b = 255, .a = 255 };
	
	SDL_Rect frame_rect = draw_sdl_inset_rect((SDL_Rect){0, 0, sp->viewport.w, sp->viewport.h}, 10);
	
#if 0
	SDL_SET_RENDER_DRAW_COLOR(renderer, &color1);
	SDL_RenderDrawRect(renderer, &frame_rect);
#else
	sdl_path_t *frame_path = sdl_path_new_from_rect(&color1, 2.0, 1,  frame_rect);
	sdl_path_render(frame_path, renderer);
	sdl_path_free(&frame_path);
#endif
	
	SDL_SET_RENDER_DRAW_COLOR(renderer, &color1);
	
	for(float x = 0.f; x <= 1.f; x += 0.1f) {
		float x1,x2, y1, y2;
		
		x1 = x2  = frame_rect.x + x * frame_rect.w;
		y1 = frame_rect.y;
		y2 = frame_rect.y + frame_rect.h;
		
		SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
	}
	
	for(float y = 0.f; y <= 1.f; y += 0.1f) {
		float x1,x2, y1, y2;
		
		x1 = frame_rect.x;
		x2 = frame_rect.x + frame_rect.w;		
		y1 = y2 = frame_rect.y + y * frame_rect.h;
		
		SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
	}
	
}

typedef struct {
	SDL_Color graticule_color;
	float subtick_len;
	
	uint32_t x_divisions;
	uint32_t x_subdivisions;
	const float *x_vals;
	const char *x_format;
	
	uint32_t y_divisions;
	uint32_t y_subdivisions;
	const float * y_vals;
	const char *y_format;
} graph_config_t;

static inline void make_graticule(scratchpad_t *sp, SDL_Renderer *renderer, const graph_config_t *config) {
	
	SDL_Color graticule_color = config->graticule_color;
	
	SDL_Rect frame_rect = draw_sdl_inset_rect((SDL_Rect){0, 0, sp->viewport.w, sp->viewport.h}, 10);
	
	SDL_SET_RENDER_DRAW_COLOR(renderer, &graticule_color);
	
	
	
	// draw horizontal divisions
	float x_inc = 1.f / config->x_divisions;
	if(config->x_subdivisions) {
		float sx_inc = x_inc / config->x_subdivisions;
		
		for(float x = 0.f; x <= 1.f; x += x_inc) {
			float x1, x2, y1, y2;
			
			for(float sx = 0.; sx <= x_inc; sx += sx_inc) {				
				x1 = x2 = frame_rect.x + (x + sx) * frame_rect.w;
				y1 = frame_rect.y + frame_rect.h - config->subtick_len;
				y2 = frame_rect.y + frame_rect.h;			
				SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);	
			}			
			
			x1 = x2  = frame_rect.x + x * frame_rect.w;
			y1 = frame_rect.y;
			y2 = frame_rect.y + frame_rect.h;			
			SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
		}	
	} else {
		for(float x = 0.f; x <= 1.f; x += x_inc) {
			
			float x1, x2, y1, y2;			
			x1 = x2  = frame_rect.x + x * frame_rect.w;
			y1 = frame_rect.y;
			y2 = frame_rect.y + frame_rect.h;			
			SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
		}		
	}
	
	// draw vertical divisions
	float y_inc = 1.f / config->y_divisions;
	if(config->y_subdivisions) {
		float sy_inc = y_inc / config->x_subdivisions;
		
		for(float y = 0.f; y <= 1.f; y += y_inc) {
			float x1, x2, y1, y2;
			
			for(float sy = 0.; sy <= y_inc; sy += sy_inc) {				
				x1 = frame_rect.x;
				x2 = frame_rect.x + config->subtick_len;		
				y1 = y2 = frame_rect.y + (y + sy) * frame_rect.h;			
				SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);	
			}
			
			x1 = frame_rect.x;
			x2 = frame_rect.x + frame_rect.w;		
			y1 = y2 = frame_rect.y + y * frame_rect.h;			
			SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
		}
	} else {
		for(float y = 0.f; y <= 1.f; y += y_inc) {
			float x1, x2, y1, y2;
			
			x1 = frame_rect.x;
			x2 = frame_rect.x + frame_rect.w;		
			y1 = y2 = frame_rect.y + y * frame_rect.h;			
			SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
		}
	}
	
	// draw frame
	sdl_path_t *frame_path = sdl_path_new_from_rect(&graticule_color, 2.0, 1,  frame_rect);
	sdl_path_render(frame_path, renderer);
	sdl_path_free(&frame_path);
}

static void make_log_graticule(scratchpad_t *sp, SDL_Renderer *renderer) {
	
	SDL_Rect frame_rect = draw_sdl_inset_rect((SDL_Rect){0, 0, sp->viewport.w, sp->viewport.h}, 10);
	SDL_Color major_color = { .r = 255, .g = 255, .b = 255, .a = 255 };
	SDL_Color minor_color = { .r = 255, .g = 255, .b = 255, .a = 255 };	
	
	float min_freq = 20.f;
	float max_freq = 22000.f;
	float min_dbfs = -60.f;
	float major_db_interval = 10.f;
	float minor_db_interval = 1.f;
	
	
	float log10_min_freq = log10f(min_freq);
	float log10_max_freq = log10f(max_freq);
	float log10_freq_scale = frame_rect.w / (log10_max_freq - log10_min_freq);
	
	{
		float freq = min_freq;
		while(freq < max_freq) {
			float log10_freq = log10f(freq);		
			float step = powf(10.f, floorf(log10_freq));
			float x = (log10_freq - log10_min_freq) * log10_freq_scale;
			
			if( freq == step ) {
				
				SDL_Rect line_rect = { frame_rect.x + x, frame_rect.y, 0.5f, frame_rect.h };
				sdl_path_t *line_path = sdl_path_new_from_rect(&major_color, 2.0, 1,  line_rect);
				sdl_path_render(line_path, renderer);
				sdl_path_free(&line_path);
				
			} else {
				float x1, x2, y1, y2;
				
				SDL_SET_RENDER_DRAW_COLOR(renderer, &minor_color);
				x1 = x2  = frame_rect.x + x;
				y1 = frame_rect.y;
				y2 = frame_rect.y + frame_rect.h;			
				SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
			}
			
			freq = step * (1.f + floorf(freq/step));
		}
		
		// draw max_freq
		{
			if( powf(10.f, floorf(log10_max_freq)) == max_freq ) {
				
				SDL_Rect line_rect = { frame_rect.x + frame_rect.w, frame_rect.y, 0.5f, frame_rect.h };
				sdl_path_t *line_path = sdl_path_new_from_rect(&major_color, 2.0, 1,  line_rect);
				sdl_path_render(line_path, renderer);
				sdl_path_free(&line_path);
				
			} else {
				float x1, x2, y1, y2;
				
				SDL_SET_RENDER_DRAW_COLOR(renderer, &minor_color);
				x1 = x2  = frame_rect.x + frame_rect.w;
				y1 = frame_rect.y;
				y2 = frame_rect.y + frame_rect.h;			
				SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
			}
		}
	}
	
	
	{
		float dbfs = 0.f;
		while (dbfs > min_dbfs) {
			float x1, x2, y1, y2;
			
			float dbfs_scale = frame_rect.h / min_dbfs;
			float y = dbfs * dbfs_scale;
			
			if( fmodf(dbfs,  major_db_interval) == 0.f ) {
				
				SDL_SET_RENDER_DRAW_COLOR(renderer, &minor_color);
				x1 = frame_rect.x;
				x2 = frame_rect.x + frame_rect.w;		
				y1 = y2 = frame_rect.y + y;			
				SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
			} else {
				SDL_SET_RENDER_DRAW_COLOR(renderer, &minor_color);
				x1 = frame_rect.x;
				x2 = frame_rect.x + 6.;		
				y1 = y2 = frame_rect.y + y;			
				SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
			}
			
			dbfs -= minor_db_interval;				
		}
	}
	
	
	// draw frame
	sdl_path_t *frame_path = sdl_path_new_from_rect(&major_color, 2.0, 1,  frame_rect);
	sdl_path_render(frame_path, renderer);
	sdl_path_free(&frame_path);
}

void scratchpad_render(scratchpad_t *sp, SDL_Renderer *renderer) {
	prepare_to_draw(sp, renderer);
	draw_wave(sp, renderer);
	
#if 0
	graph_config_t graph_config = {
		.graticule_color = { .r = 255, .g = 255, .b = 255, .a = 255 },
		.subtick_len = 10.f,
		
		.x_divisions = 10,
		.x_subdivisions = 5,
		.x_vals = NULL,
		.x_format = "%0.2f",
		
		.y_divisions = 10,
		.y_subdivisions = 5,
		.y_vals = NULL,
		.y_format = "%0.2f",
	};
	make_graticule(sp, renderer, &graph_config);
#endif
	
#if 0
	make_log_graticule(sp, renderer);
#endif

	if(sp->frg) {
		fresp_graph_render(sp->frg, renderer);
	}	
}
