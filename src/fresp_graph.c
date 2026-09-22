#include "fresp_graph.h"
#include "s9.h"
#include "s9_font.h"
#include "draw_sdl.h"
#include "graph_axis.h"
#include <stdlib.h>

typedef struct fresp_graph {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	
	fresp_graph_config_t config;
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

static graph_hz_axis_t hz_axis(const fresp_graph_t *frg) {
	return (graph_hz_axis_t){
		.min_hz = frg->config.min_freq,
		.max_hz = frg->config.max_freq,
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
		graph_hz_axis_t axis = hz_axis(frg);
		int height, right;
		graph_hz_axis_measure(&axis, font, &height, &right);
		if(bottom_margin < height) {
			bottom_margin = height;
		}
		if(right_margin < right) {
			right_margin = right;
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

	// draw frequency graticule
	{
		graph_hz_axis_t axis = hz_axis(frg);
		graph_hz_axis_draw(&axis, renderer, font, &frg->plot_rect, LABEL_MARGIN);
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
	SDL_GetRenderViewport(renderer, &viewport);
	
	if(frg->renderer != renderer || !SDL_RectsEqual(&frg->viewport, &viewport)) {
		frg->renderer = renderer;
		frg->viewport = viewport;
		
		if(frg->graticule_texture) {
			SDL_DestroyTexture(frg->graticule_texture);
			frg->graticule_texture = NULL;
		}
		
		frg->graticule_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, frg->viewport.w, frg->viewport.h);
		CHECK_ALLOC(frg->graticule_texture, return);
		
		bool ret = SDL_SetRenderTarget(renderer, frg->graticule_texture);
		CHECK_ERROR(ret,
					SDL_DestroyTexture(frg->graticule_texture);
					frg->graticule_texture = NULL;
					return,
					"could not set render target");
		
		ret = SDL_SetTextureBlendMode(frg->graticule_texture, SDL_BLENDMODE_BLEND);
		CHECK_ERROR(ret,
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
			SDL_GetRenderViewport(renderer, &save_viewport);			
			
			// constrain viewport to plot_rect
			SDL_Rect plot_viewport = frg->plot_rect;
			plot_viewport.x += save_viewport.x;
			plot_viewport.y += save_viewport.y;			
			SDL_SetRenderViewport(renderer, &plot_viewport);
			
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
			SDL_SetRenderViewport(renderer, &save_viewport);
		}
	}
	
	SDL_FRect dst_rect = { 0.0f, 0.0f, (float)frg->viewport.w, (float)frg->viewport.h };
	SDL_RenderTexture(renderer, frg->graticule_texture, NULL, &dst_rect);
}
