#ifndef fresp_graph_h
#define fresp_graph_h

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

typedef struct fresp_graph fresp_graph_t;



typedef struct {
	uint32_t min_freq;
	uint32_t max_freq;
	int32_t  min_dbfs;
	int32_t  max_dbfs;
	uint32_t major_db_interval;
	uint32_t minor_db_interval;
	SDL_Color major_color;
	SDL_Color minor_color;
} fresp_graph_config_t;

static const fresp_graph_config_t default_fresp_graph_config = {
	.min_freq = 20,
	.max_freq = 22000,
	.min_dbfs = -60,
	.max_dbfs = 0,
	.major_db_interval = 10,
	.minor_db_interval = 1,
	.major_color = { .r = 255, .g = 255, .b = 255, .a = 255 },
	.minor_color = { .r = 255, .g = 255, .b = 255, .a = 255 },
};

// create fresp_graph
fresp_graph_t *fresp_graph_new(fresp_graph_config_t *config, TTF_Font* (*load_font)(int point_size));

// destroy fresp_graph
void fresp_graph_free(fresp_graph_t **frg);

// render fresp_graph
void fresp_graph_render(fresp_graph_t *frg, SDL_Renderer *renderer);

#endif /* fresp_graph_h */
