#include "fresp_graph.h"
#include "s9.h"
#include "s9_font.h"
#include "draw_sdl.h"
#include "graph_axis.h"

typedef struct fresp_graph {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	
	fresp_graph_config_t config;
	float log10_min_freq;
	float log10_max_freq;
	
	float log10_freq_scale;
	int font_size;
	SDL_Rect plot_rect;
	SDL_Texture *graticule_texture;
	float *plot_data_buffer;
	char *label;                     // in the plot's top left corner, or NULL

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

fresp_graph_t *fresp_graph_new(fresp_graph_config_t *config) {
	fresp_graph_t *frg = (fresp_graph_t *) malloc(sizeof(fresp_graph_t));
	CHECK_ALLOC(frg, return NULL);
	memset(frg, 0, sizeof(*frg));
	fresp_graph_set_config(frg, config);
	
	return frg;
}

void fresp_graph_free(fresp_graph_t **frg) {
	if (frg && *frg) {
		
		if((*frg)->graticule_texture) {
			SDL_DestroyTexture((*frg)->graticule_texture);
			(*frg)->graticule_texture = NULL;
		}
		
		if((*frg)->plot_data_buffer) {
			free((*frg)->plot_data_buffer);
			(*frg)->plot_data_buffer = NULL;
		}
		free((*frg)->label);

		free(*frg);
		*frg = NULL;
	}
}

void fresp_graph_set_label(fresp_graph_t *frg, const char *label) {
	if ((label == NULL) != (frg->label == NULL) || (label && strcmp(label, frg->label) != 0)) {
		free(frg->label);
		frg->label = label ? strdup(label) : NULL;
		frg->renderer = NULL;   // the graticule is drawn again with it
	}
}

static graph_db_axis_t db_axis(const fresp_graph_t *frg) {
	return (graph_db_axis_t){
		.min_db = frg->config.min_db,
		.max_db = frg->config.max_db,
		.major_interval = frg->config.major_db_interval,
		.minor_interval = frg->config.minor_db_interval,
		.major_color = frg->config.major_color,
		.minor_color = frg->config.minor_color,
	};
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
				char label[32];
				graph_format_hz(label, sizeof(label), freq);
				int width, height;
				int ret = TTF_SizeUTF8(font, label, &width, &height);
				if(ret == 0) {
					if(bottom_margin < height) {
						bottom_margin = height;
					}
				}
				CHECK_ERROR(ret == 0, , "failed to measure string \"%s\"", label);
			}
			
			freq = step * (1.f + floorf(freq / step));
		}
		
		// handle max_freq
		if(powf(10.f, floorf(frg->log10_max_freq)) == frg->config.max_freq) {
			// major tick
			char label[32];
			graph_format_hz(label, sizeof(label), frg->config.max_freq);
			int width, height;
			int ret = TTF_SizeUTF8(font, label, &width, &height);
			if(ret == 0) {
				if(bottom_margin < height) {
					bottom_margin = height;
				}
				
				if(right_margin < width / 2) {
					right_margin = width / 2;
				}
			}
			CHECK_ERROR(ret == 0, , "failed to measure string \"%s\"", label);
		}
	}
	
	// set margins to make room for db labels
	{
		graph_db_axis_t axis = db_axis(frg);
		int width, height;
		graph_db_axis_measure(&axis, font, &width, &height);
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
	
	top_margin    += GRAPH_MARGIN;
	left_margin   += GRAPH_MARGIN + LABEL_MARGIN;
	bottom_margin += GRAPH_MARGIN + LABEL_MARGIN;
	right_margin  += GRAPH_MARGIN;
	
	frg->plot_rect = (SDL_Rect) { .x = left_margin, .y = top_margin, .w = frg->viewport.w - (left_margin + right_margin), .h = frg->viewport.h - (top_margin + bottom_margin) };
}

static void draw_graticule(fresp_graph_t *frg, SDL_Renderer *renderer) {
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // clear background
	SDL_RenderClear(renderer);
	
	TTF_Font *font = s9_open_font(16);
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
				float line_width = 2.0;
				sdl_polygon_t *line = rect_new(frg->plot_rect.x + x - line_width * 0.5f,
											   frg->plot_rect.y - line_width * 0.5f,
											   line_width,
											   frg->plot_rect.h + line_width,
											   frg->config.major_color);
				if(line) {
					SDL_RENDER_GEOMETRY_POLYGON(renderer, line);
					sdl_polygon_free(&line);
				}
				
				char label[32];
				graph_format_hz(label, sizeof(label), freq);
				draw_sdl_render_text_at_position(renderer,
										font,
										label,
										ANCHOR_TOP_MIDDLE,
										frg->config.major_color,
										frg->plot_rect.x + x,
										frg->plot_rect.y + frg->plot_rect.h + LABEL_MARGIN);
				
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
			float line_width = 2.0;
			sdl_polygon_t *line = rect_new(frg->plot_rect.x + frg->plot_rect.w - line_width * 0.5f,
										   frg->plot_rect.y - line_width * 0.5f,
										   line_width,
										   frg->plot_rect.h + line_width,
										   frg->config.major_color);
			if(line) {
				SDL_RENDER_GEOMETRY_POLYGON(renderer, line);
				sdl_polygon_free(&line);
			}

			char label[32];
			graph_format_hz(label, sizeof(label), frg->config.max_freq);
			draw_sdl_render_text_at_position(renderer,
									font,
									label,
									ANCHOR_TOP_MIDDLE,
									frg->config.major_color,
									frg->plot_rect.x + frg->plot_rect.w,
									frg->plot_rect.y + frg->plot_rect.h + LABEL_MARGIN);
			
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
		graph_db_axis_t axis = db_axis(frg);
		graph_db_axis_draw(&axis, renderer, font, &frg->plot_rect, LABEL_MARGIN);
	}
	
	// draw frame
	sdl_polygon_t *frame = frame_new_from_rect(frg->plot_rect, 2.0, frg->config.major_color);
	if(frame) {
		SDL_RENDER_GEOMETRY_POLYGON(renderer, frame);
		sdl_polygon_free(&frame);
	}

	// draw the label, inside the plot's top left corner, clear of the dB axis's ticks
	if(frg->label) {
		draw_sdl_render_text_at_position(renderer, font, frg->label, ANCHOR_TOP_LEFT, frg->config.major_color,
										 frg->plot_rect.x + GRAPH_DB_AXIS_TICK_LENGTH + 4, frg->plot_rect.y + 4);
	}

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
		
		// free old plot_data_buffer 
		if(frg->plot_data_buffer) {
			free(frg->plot_data_buffer);
			frg->plot_data_buffer = NULL;
		}
		
		// allocate plot_data_buffer
		frg->plot_data_buffer = malloc(sizeof(float) * frg->plot_rect.w);
		CHECK_ALLOC(frg->plot_data_buffer,
					SDL_DestroyTexture(frg->graticule_texture);
					frg->graticule_texture = NULL;
					return;
					);		
	}
}

void fresp_graph_render(fresp_graph_t *frg, SDL_Renderer *renderer, fresp_graph_data_callback_t get_plot_data, void *get_data_callback_context) {
	prepare_to_draw(frg, renderer);
	if(!frg->graticule_texture) return;
	
	if(get_plot_data) {						
		if( 0  == get_plot_data(get_data_callback_context, frg->plot_data_buffer, frg->plot_rect.w) ) {
			// save current viewport
			SDL_Rect save_viewport;
			SDL_RenderGetViewport(renderer, &save_viewport);			
			
			// constrain viewport to plot_rect
			SDL_Rect plot_viewport = frg->plot_rect;
			plot_viewport.x += save_viewport.x;
			plot_viewport.y += save_viewport.y;			
			SDL_RenderSetViewport(renderer, &plot_viewport);
			
			// create path
			SDL_FPoint *points = malloc(sizeof(SDL_FPoint) * frg->plot_rect.w);			
			float db_scale = (float)frg->plot_rect.h / (frg->config.max_db - frg->config.min_db);
			
			for(int i=0; i < frg->plot_rect.w; i++) {
				float y = (frg->config.max_db - frg->plot_data_buffer[i]) * db_scale;
				
				points[i].x = i;
				points[i].y = y;
			}						
			sdl_path_t *path = sdl_path_new(&frg->config.plot_color, 2.f, 1, points, frg->plot_rect.w);
			free(points);
			
			// render path
			sdl_path_render(path, renderer);
			
			// free path
			sdl_path_free(&path);
			
			// restore viewport
			SDL_RenderSetViewport(renderer, &save_viewport);
		}
	}
	
	SDL_Rect dst_rect = { 0, 0, frg->viewport.w, frg->viewport.h };
	SDL_RenderCopy(renderer, frg->graticule_texture, NULL, &dst_rect);
}
