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
	
	float log10_freq_scale;
	int font_size;
	SDL_Rect plot_rect;
	SDL_Texture *graticule_texture;
	
} fresp_graph_t;

// margin between edge of labels and plot_rect
#define LABEL_MARGIN 8

// margin between entire graph content and viewport
#define GRAPH_MARGIN 10

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

// asprintf implementation, for portability
static int fresp_graph_asprintf(char **strp, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int size = vsnprintf(NULL, 0, fmt, args);
	va_end(args);
	
	if (size < 0) return -1;
	
	*strp = (char *)malloc(size + 1);
	if (!*strp) return -1;
	
	va_start(args, fmt);
	int result = vsnprintf(*strp, size + 1, fmt, args);
	va_end(args);
	
	if (result < 0) {
		free(*strp);
		*strp = NULL;
	}
	
	return result;
}

// allocates and returns a Hz/KHz/MHz/GHz string
static char* create_hz_string(long int freq) {
	char *ret = NULL;
	
	if(freq >= 1000000000) {
		if(fmodf(freq, 1000000000.f) != 0.f) {
			fresp_graph_asprintf(&ret, "%0.3f GHz", freq / 1000000000.f);
		} else {
			fresp_graph_asprintf(&ret, "%d GHz", freq / 1000000000);
		}
	} else if(freq >= 1000000) {
		if(fmodf(freq, 1000000.f) != 0.f) {
			fresp_graph_asprintf(&ret, "%0.3f MHz", freq / 1000000.f);
		} else {
			fresp_graph_asprintf(&ret, "%d MHz", freq / 1000000);
		}
	} else if(freq >= 1000) {
		if(fmodf(freq, 1000.f) != 0.f) {
			fresp_graph_asprintf(&ret, "%0.3f KHz", freq / 1000.f);
		} else {
			fresp_graph_asprintf(&ret, "%d KHz", freq / 1000);
		}
	} else {
		if(fmodf(freq, 1.f) != 0.f) {
			fresp_graph_asprintf(&ret, "%0.3f Hz", freq);
		} else {
			fresp_graph_asprintf(&ret, "%d Hz", freq);
		}
	}
	
	return ret;
}

static char* create_db_string(int db) {
	char *ret = NULL;
	fresp_graph_asprintf(&ret, "%d dB", db);
	
	return ret;
}

// measures unit labels and sets plot_rect member accordingly
static void compute_plot_rect(fresp_graph_t *frg, TTF_Font *font) {
	int top_margin = 0;
	int left_margin = 0;
	int bottom_margin = 0;
	int right_margin = 0;
	
	// set margins to make room for frequency labels 
	{
		float freq = frg->config.min_freq;
		while(freq < frg->config.max_freq) {
			float log10_freq = log10f(freq);
			float step = powf(10.f, floorf(log10_freq));
			
			if(freq == step) {
				// major tick
				int ret = -1;
				char *str = create_hz_string(freq);
				if(str) {
					int width, height;
					ret = TTF_SizeUTF8(font, str, &width, &height);
					if(ret == 0) {
						if(bottom_margin < height) {
							bottom_margin = height;
						}
					}
					free(str);
					str = NULL;
				}
				CHECK_ERROR(ret == 0, , "failed to measure string \"%s\"", str);
			}
			
			freq = step * (1.f + floorf(freq / step));
		}
		
		// handle max_freq
		if(powf(10.f, floorf(frg->log10_max_freq)) == frg->config.max_freq) {
			// major tick
			int ret = -1;
			char *str = create_hz_string(frg->config.max_freq);
			if(str) {
				int width, height;
				ret = TTF_SizeUTF8(font, str, &width, &height);
				if(ret == 0) {
					if(bottom_margin < height) {
						bottom_margin = height;
					}
					
					if(right_margin < width / 2) {
						right_margin = width / 2;
					}
				}
				free(str);
				str = NULL;
			}
			CHECK_ERROR(ret == 0, , "failed to measure string \"%s\"", str);
		}
	}
	
	// set margins to make room for db labels
	{
		int db = frg->config.max_db;
		while (db >= frg->config.min_db) {
			if(db % (int)frg->config.major_db_interval == 0) {
				// major tick
				int ret = -1;
				char *str = create_db_string((int)db);
				if(str) {
					int width, height;
					ret = TTF_SizeUTF8(font, str, &width, &height);
					if(ret == 0) {
						if(left_margin < width) {
							left_margin = width;
						}
						
						if(top_margin < height / 2) {
							top_margin = height / 2;
						}
						
						if(bottom_margin < height / 2) {
							bottom_margin = height / 2;
						}
					}
					free(str);
					str = NULL;
				}
				CHECK_ERROR(ret == 0, , "failed to measure string \"%s\"", str);
			}
			
			db--;
		}
	}
	
	top_margin    += GRAPH_MARGIN;
	left_margin   += GRAPH_MARGIN + LABEL_MARGIN;
	bottom_margin += GRAPH_MARGIN + LABEL_MARGIN;
	right_margin  += GRAPH_MARGIN;
	
	frg->plot_rect = (SDL_Rect) { .x = left_margin, .y = top_margin, .w = frg->viewport.w - (left_margin + right_margin), .h = frg->viewport.h - (top_margin + bottom_margin) };
}

static void draw_graticule(fresp_graph_t *frg, SDL_Renderer *renderer) {
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // clear background
	SDL_RenderClear(renderer);
	
	TTF_Font *font = frg->load_font(16);
	CHECK_ERROR(font != NULL, return, "unable to create font");
	
	compute_plot_rect(frg, font);
	
	frg->log10_freq_scale = frg->plot_rect.w / (frg->log10_max_freq - frg->log10_min_freq);
	
	// draw frequency graticule
	{
		float freq = frg->config.min_freq;
		while(freq < frg->config.max_freq) {
			float log10_freq = log10f(freq);
			float step = powf(10.f, floorf(log10_freq));
			float x = (log10_freq - frg->log10_min_freq) * frg->log10_freq_scale;
			
			if(freq == step) {
				// major tick
				SDL_Rect line_rect = { frg->plot_rect.x + x, frg->plot_rect.y, 0.5f, frg->plot_rect.h };
				sdl_path_t *line_path = sdl_path_new_from_rect(&frg->config.major_color, 2.0, 1, line_rect);
				sdl_path_render(line_path, renderer);
				sdl_path_free(&line_path);
				
				char *label = create_hz_string(freq);
				if(label) {
					draw_sdl_render_text_at_position(renderer,
											font,
											label,
											POSITION_TOP_MIDDLE,
											frg->config.major_color,
											frg->plot_rect.x + x,
											frg->plot_rect.y + frg->plot_rect.h + LABEL_MARGIN);
					free(label);
					label = NULL;
				}
				
			} else {
				float x1, x2, y1, y2;
				// minor tick
				SDL_SET_RENDER_DRAW_COLOR(renderer, &frg->config.minor_color);
				x1 = x2  = frg->plot_rect.x + x;
				y1 = frg->plot_rect.y;
				y2 = frg->plot_rect.y + frg->plot_rect.h;
				SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
			}
			
			freq = step * (1.f + floorf(freq / step));
		}
		
		// draw max_freq
		if(powf(10.f, floorf(frg->log10_max_freq)) == frg->config.max_freq) {
			// major tick
			SDL_Rect line_rect = { frg->plot_rect.x + frg->plot_rect.w, frg->plot_rect.y, 0.5f, frg->plot_rect.h };
			sdl_path_t *line_path = sdl_path_new_from_rect(&frg->config.major_color, 2.0, 1, line_rect);
			sdl_path_render(line_path, renderer);
			sdl_path_free(&line_path);
			
			char *label = create_hz_string(frg->config.max_freq);
			if(label) {
				draw_sdl_render_text_at_position(renderer,
										font,
										label,
										POSITION_TOP_MIDDLE,
										frg->config.major_color,
										frg->plot_rect.x + frg->plot_rect.w,
										frg->plot_rect.y + frg->plot_rect.h + LABEL_MARGIN);
				free(label);
				label = NULL;
			}
			
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
	
	// draw db graticule
	{
		float db_scale = (float)frg->plot_rect.h / (frg->config.max_db - frg->config.min_db);
		
		int db = frg->config.max_db;
		while (db >= frg->config.min_db) {
			float x1, x2, y1, y2;
			
			float y = (frg->config.max_db - db) * db_scale;
			
			if(db % (int)frg->config.major_db_interval == 0) {
				// major tick
				SDL_SET_RENDER_DRAW_COLOR(renderer, &frg->config.minor_color);
				x1 = frg->plot_rect.x;
				x2 = frg->plot_rect.x + frg->plot_rect.w;
				y1 = y2 = frg->plot_rect.y + y;
				SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
				
				char *label = create_db_string(db);
				if(label) {
					draw_sdl_render_text_at_position(renderer,
											font,
											label,
											POSITION_MIDDLE_RIGHT,
											frg->config.major_color,
											frg->plot_rect.x - LABEL_MARGIN,
											frg->plot_rect.y + y);
					free(label);
					label = NULL;
				}
			} else if(db % (int)frg->config.minor_db_interval == 0) {
				// minor tick
				SDL_SET_RENDER_DRAW_COLOR(renderer, &frg->config.minor_color);
				x1 = frg->plot_rect.x;
				x2 = frg->plot_rect.x + 6.;
             	y1 = y2 = frg->plot_rect.y + y;
				SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
			}
			
			db--;
		}
	}
	
	// draw frame
	sdl_path_t *frame_path = sdl_path_new_from_rect(&frg->config.major_color, 2.0, 1, frg->plot_rect);
	sdl_path_render(frame_path, renderer);
	sdl_path_free(&frame_path);
	
	TTF_CloseFont(font);
}

static S9_FORCE_INLINE void prepare_to_draw(fresp_graph_t *frg, SDL_Renderer *renderer) {
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
		
		int ret = SDL_SetRenderTarget(renderer, frg->graticule_texture);
		CHECK_ERROR(ret == 0,
					SDL_DestroyTexture(frg->graticule_texture);
					frg->graticule_texture = NULL;
					return,
					"could not set render target");
		
		ret = SDL_SetTextureBlendMode(frg->graticule_texture, SDL_BLENDMODE_BLEND);
		CHECK_ERROR(ret == 0,
					SDL_DestroyTexture(frg->graticule_texture);
					frg->graticule_texture = NULL;
					return,
					"could not set blend mode");
		
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
