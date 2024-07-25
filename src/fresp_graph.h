#ifndef fresp_graph_h
#define fresp_graph_h

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "s9_colors.h"

typedef struct fresp_graph fresp_graph_t;

typedef struct {
	uint32_t min_freq;
	uint32_t max_freq;
	int32_t  min_db;
	int32_t  max_db;
	uint32_t major_db_interval;
	uint32_t minor_db_interval;
	SDL_Color major_color;
	SDL_Color minor_color;
	SDL_Color plot_color;
} fresp_graph_config_t;

static const fresp_graph_config_t default_fresp_graph_config = {
	.min_freq = 20,
	.max_freq = 22000,
	.min_db = -60,
	.max_db = 0,
	.major_db_interval = 10,
	.minor_db_interval = 1,
	.major_color = S9_COCONUT,
	.minor_color = S9_COCONUT,
	.plot_color = S9_RASPBERRY,
};

// create fresp_graph
fresp_graph_t *fresp_graph_new(fresp_graph_config_t *config, TTF_Font* (*load_font)(int point_size));

// destroy fresp_graph
void fresp_graph_free(fresp_graph_t **frg);

// callback function invoked by fresp_graph_render()
// fills the points array with db values to plot, distributed logarithmically (base 10) between the configured min_freq and max_freq 
// returns 0 on success
typedef int (*fresp_graph_data_callback_t)(void *context, float *points, uint32_t n_points);

// render fresp_graph
void fresp_graph_render(fresp_graph_t *frg, SDL_Renderer *renderer, fresp_graph_data_callback_t get_data_callback, void *get_data_callback_context);

#endif /* fresp_graph_h */
