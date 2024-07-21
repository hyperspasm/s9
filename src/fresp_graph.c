#include "fresp_graph.h"
#include "s9.h"
#include "draw_sdl.h"

typedef struct fresp_graph {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	
	fresp_graph_config_t config;
	TTF_Font* (*load_font)(int point_size);
	float log10_min_freq;
	float log10_max_freq;
		
	SDL_Texture *graticule_texture;
	float log10_freq_scale;
	SDL_Rect plot_rect;
	
	// TODO
	
} fresp_graph_t;

void fresp_graph_set_config(fresp_graph_t *frg, fresp_graph_config_t *config) {
	if(config) {
		frg->config = *config;
	} else {
		frg->config = default_fresp_graph_config;
	}
	
	frg->log10_min_freq = log10f(frg->config.min_freq);
	frg->log10_max_freq = log10f(frg->config.max_freq);
	
	frg->renderer = NULL;
}

fresp_graph_t *fresp_graph_new(fresp_graph_config_t *config, TTF_Font* (*load_font)(int point_size)) {
	fresp_graph_t *frg = (fresp_graph_t *) malloc(sizeof(fresp_graph_t));
	CHECK_ALLOC(frg, return NULL);
	memset(frg, 0, sizeof(*frg));
	frg->load_font = load_font;
	fresp_graph_set_config(frg, config);
	
	return frg;
}

void fresp_graph_free(fresp_graph_t **frg) {
	if (frg && *frg) {
		
		if((*frg)->graticule_texture) {
			SDL_DestroyTexture((*frg)->graticule_texture);			
			(*frg)->graticule_texture = NULL;
		}
		
		free(*frg);
		*frg = NULL;
	}
}

static void draw_graticule(fresp_graph_t *frg, SDL_Renderer *renderer) {
	
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // clear background
	SDL_RenderClear(renderer);
	
	frg->plot_rect = draw_sdl_inset_rect((SDL_Rect){0, 0, frg->viewport.w, frg->viewport.h}, 10);	
	frg->log10_freq_scale = frg->plot_rect.w / (frg->log10_max_freq - frg->log10_min_freq);
	
	{
		float freq = frg->config.min_freq;
		while(freq < frg->config.max_freq) {
			float log10_freq = log10f(freq);		
			float step = powf(10.f, floorf(log10_freq));
			float x = (log10_freq - frg->log10_min_freq) * frg->log10_freq_scale;
			
			if( freq == step ) {
				// major tick
				SDL_Rect line_rect = { frg->plot_rect.x + x, frg->plot_rect.y, 0.5f, frg->plot_rect.h };
				sdl_path_t *line_path = sdl_path_new_from_rect(&frg->config.major_color, 2.0, 1,  line_rect);
				sdl_path_render(line_path, renderer);
				sdl_path_free(&line_path);
				
			} else {
				float x1, x2, y1, y2;
				// minor tick
				SDL_SET_RENDER_DRAW_COLOR(renderer, &frg->config.minor_color);
				x1 = x2  = frg->plot_rect.x + x;
				y1 = frg->plot_rect.y;
				y2 = frg->plot_rect.y + frg->plot_rect.h;			
				SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
			}
			
			freq = step * (1.f + floorf(freq/step));
		}
		
		// draw max_freq
		{
			if( powf(10.f, floorf(frg->log10_max_freq)) == frg->config.max_freq ) {
				// major tick
				SDL_Rect line_rect = { frg->plot_rect.x + frg->plot_rect.w, frg->plot_rect.y, 0.5f, frg->plot_rect.h };
				sdl_path_t *line_path = sdl_path_new_from_rect(&frg->config.major_color, 2.0, 1,  line_rect);
				sdl_path_render(line_path, renderer);
				sdl_path_free(&line_path);
				
			} else {
				float x1, x2, y1, y2;
				// minor tick
				SDL_SET_RENDER_DRAW_COLOR(renderer, &frg->config.minor_color);
				x1 = x2  = frg->plot_rect.x + frg->plot_rect.w;
				y1 = frg->plot_rect.y;
				y2 = frg->plot_rect.y + frg->plot_rect.h;			
				SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
			}
		}
	}
	
	
	{
		float dbfs = 0.f;
		while (dbfs > frg->config.min_dbfs) {
			float x1, x2, y1, y2;
			
			float dbfs_scale = frg->plot_rect.h / frg->config.min_dbfs;
			float y = dbfs * dbfs_scale;
			
			if( fmodf(dbfs,  frg->config.major_db_interval) == 0.f ) {
				// major tick
				SDL_SET_RENDER_DRAW_COLOR(renderer, &frg->config.minor_color);
				x1 = frg->plot_rect.x;
				x2 = frg->plot_rect.x + frg->plot_rect.w;		
				y1 = y2 = frg->plot_rect.y + y;			
				SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
			} else {
				// minor tick
				SDL_SET_RENDER_DRAW_COLOR(renderer, &frg->config.minor_color);
				x1 = frg->plot_rect.x;
				x2 = frg->plot_rect.x + 6.;		
				y1 = y2 = frg->plot_rect.y + y;			
				SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
			}
			
			dbfs -= frg->config.minor_db_interval;				
		}
	}
	
	
	// draw frame
	sdl_path_t *frame_path = sdl_path_new_from_rect(&frg->config.major_color, 2.0, 1,  frg->plot_rect);
	sdl_path_render(frame_path, renderer);
	sdl_path_free(&frame_path);
}

static S9_FORCE_INLINE void prepare_to_draw(fresp_graph_t *frg, SDL_Renderer *renderer) {
	int ret;
	SDL_Rect viewport;	
	SDL_RenderGetViewport(renderer, &viewport);
	
	if(frg->renderer != renderer || !SDL_RectEquals(&frg->viewport, &viewport)) {
		frg->renderer = renderer;
		frg->viewport = viewport;		
		
		if(frg->graticule_texture) {
			SDL_DestroyTexture(frg->graticule_texture);			
			frg->graticule_texture = NULL;
		}
		
		
		frg->graticule_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, frg->viewport.w, frg->viewport.h);
		CHECK_ALLOC(frg->graticule_texture, return);
		
		ret = SDL_SetRenderTarget(renderer, frg->graticule_texture);
		CHECK_ERROR(ret == 0,
					SDL_DestroyTexture(frg->graticule_texture);
					frg->graticule_texture = NULL;
					return,
					"Could not set render target");
		
		ret = SDL_SetTextureBlendMode(frg->graticule_texture, SDL_BLENDMODE_BLEND);
		CHECK_ERROR(ret == 0,
					SDL_DestroyTexture(frg->graticule_texture);
					frg->graticule_texture = NULL;
					return,
					"Could not set blend mode");
		
		draw_graticule(frg, renderer);
		
		// reset render target
		SDL_SetRenderTarget(renderer, NULL);		
	}
}

void fresp_graph_render(fresp_graph_t *frg, SDL_Renderer *renderer) {
	prepare_to_draw(frg, renderer);	
	if(!frg->graticule_texture) return;
	
	SDL_Rect dst_rect = { 0, 0, frg->viewport.w, frg->viewport.h };
	SDL_RenderCopy(renderer, frg->graticule_texture, NULL, &dst_rect);
	
}
