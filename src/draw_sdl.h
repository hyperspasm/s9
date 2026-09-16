#ifndef draw_sdl_h
#define draw_sdl_h

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

typedef enum {
	ANCHOR_TOP_LEFT,
	ANCHOR_TOP_MIDDLE,
	ANCHOR_TOP_RIGHT,
	ANCHOR_MIDDLE_LEFT,
	ANCHOR_MIDDLE_RIGHT,
	ANCHOR_BOTTOM_LEFT,
	ANCHOR_BOTTOM_MIDDLE,
	ANCHOR_BOTTOM_RIGHT,
	ANCHOR_MIDDLE,
} anchor_position_t;

// -- general

#define SDL_SET_RENDER_DRAW_COLOR(renderer, color) \
	SDL_SetRenderDrawColor((renderer), (color)->r, (color)->g, (color)->b, (color)->a)

// clear the current viewport with a given background color, or black in color is NULL
void draw_sdl_clear_viewport(SDL_Renderer *renderer, SDL_Color *color);

// blits a texture to the renderer at the specified position with the given anchor
void draw_sdl_blit_texture(SDL_Renderer *renderer, SDL_Texture *texture, float x, float y, anchor_position_t anchor);

// -- paths 

// A path is a defined by a series of points.
// When the first and last points of a path are equal it is considered closed.
typedef struct sdl_path sdl_path_t;

// create a new path
sdl_path_t *sdl_path_new(SDL_Color *color, float width, int filled, const SDL_FPoint *path_points, int count);

void sdl_path_free(sdl_path_t **path);

// render the path
int sdl_path_render(sdl_path_t *path, SDL_Renderer *renderer);


// -- text

// renders text at specified position with and alignment
int draw_sdl_render_text_at_position(SDL_Renderer *renderer, TTF_Font *font, const char *text, anchor_position_t anchor, SDL_Color color, int x, int y);

// renders text to an SDL texture, updating the texture and optionally outputting the text dimensions
int draw_sdl_render_text(SDL_Renderer *renderer, SDL_Texture **texture, TTF_Font *font, SDL_Color *color, const char *text, int *out_width, int *out_height);


// -- polygons

// struct to represent a polygon with vertices and indices
typedef struct {
	SDL_Vertex *vertices;   // array of vertices
	uint32_t num_vertices;  // number of vertices
	int *indices;           // array of indices
	uint32_t num_indices;   // number of indices
} sdl_polygon_t;

// macro to render an sdl_polygon_t using SDL_RenderGeometry
#define SDL_RENDER_GEOMETRY_POLYGON(renderer, poly) \
	SDL_RenderGeometry(renderer, NULL, (poly)->vertices, (poly)->num_vertices, (poly)->indices, (poly)->num_indices)

// frees the memory allocated for an sdl_polygon_t
void sdl_polygon_free(sdl_polygon_t **poly);

// offsets the position of all vertices in an sdl_polygon_t by the given x and y amounts
void sdl_polygon_offset(sdl_polygon_t *poly, float x, float y);

// creates a texture from an sdl_polygon_t
SDL_Texture* sdl_polygon_render_to_texture(SDL_Renderer *renderer, sdl_polygon_t *poly);

// creates a new sdl_polygon_t representing a circle
sdl_polygon_t* circle_new(float r, SDL_Color *color);

// creates a new sdl_polygon_t representing a frame
sdl_polygon_t* frame_new(float x, float y, float width, float height, float edge_width, SDL_Color color);

// macro to create a new frame from an SDL_Rect
#define frame_new_from_rect(rect, edge_width, color) \
	frame_new((rect).x, (rect).y, (rect).w, (rect).h, edge_width, color)

// function to create an sdl_polygon_t representing a rectangle
sdl_polygon_t* rect_new(float x, float y, float width, float height, SDL_Color color);

// macro to create a new rectangle from an SDL_Rect
#define rect_new_from_rect(rect, color) \
	rect_new((rect).x, (rect).y, (rect).w, (rect).h, color)


// -- geometry

// inset a rect
SDL_Rect draw_sdl_inset_rect(SDL_Rect rect, float inset);

// offsets the position of each vertex by the given x and y amounts
void draw_sdl_offset_vertices(SDL_Vertex *vertices, uint32_t n, float offset_x, float offset_y);

// offsets the position of each point by the given x and y amounts
void draw_sdl_offset_points(SDL_FPoint *points, uint32_t n, float offset_x, float offset_y);

#endif /* draw_sdl_h */
