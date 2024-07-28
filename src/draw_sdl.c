#include "draw_sdl.h"
#include "s9.h"
#include "s9_colors.h"


void draw_sdl_clear_viewport(SDL_Renderer *renderer, SDL_Color *color) {
	SDL_Rect viewport;	
	SDL_RenderGetViewport(renderer, &viewport);	
	SDL_Rect fillRect = { .x = 0, .y = 0, .w = viewport.w, .h = viewport.h };
	
	SDL_Color clearColor = S9_BLACK;		
	if(color) {
		clearColor = *color;
	}
	
	SDL_SetRenderDrawColor(renderer, clearColor.r, clearColor.g, clearColor.b, clearColor.a);
	SDL_RenderFillRect(renderer, &fillRect);
}

void draw_sdl_blit_texture(SDL_Renderer *renderer, SDL_Texture *texture, float x, float y, anchor_position_t anchor) {
	CHECK_PARAMETER(renderer != NULL, return);
	CHECK_PARAMETER(texture != NULL, return);
	
	int texture_width, texture_height;
	SDL_QueryTexture(texture, NULL, NULL, &texture_width, &texture_height);
	
	int render_x = (int)x;
	int render_y = (int)y;
	
	switch (anchor) {
		case ANCHOR_TOP_LEFT:
			break;
		case ANCHOR_TOP_MIDDLE:
			render_x -= texture_width / 2;
			break;
		case ANCHOR_TOP_RIGHT:
			render_x -= texture_width;
			break;
		case ANCHOR_MIDDLE_LEFT:
			render_y -= texture_height / 2;
			break;
		case ANCHOR_MIDDLE_RIGHT:
			render_x -= texture_width;
			render_y -= texture_height / 2;
			break;
		case ANCHOR_BOTTOM_LEFT:
			render_y -= texture_height;
			break;
		case ANCHOR_BOTTOM_MIDDLE:
			render_x -= texture_width / 2;
			render_y -= texture_height;
			break;
		case ANCHOR_BOTTOM_RIGHT:
			render_x -= texture_width;
			render_y -= texture_height;
			break;
		case ANCHOR_MIDDLE:
			render_x -= texture_width / 2;
			render_y -= texture_height / 2;
			break;
	}
	
	SDL_Rect dst_rect = { render_x, render_y, texture_width, texture_height };
	SDL_RenderCopy(renderer, texture, NULL, &dst_rect);
}

// computes a vector of specified length perpendicular to the vector from p1 to p0
static inline void compute_perpendicular_vector(const SDL_FPoint *p0, const SDL_FPoint *p1, float length, float *out_dx, float *out_dy) {
	float dx = p0->y - p1->y;
	float dy = p0->x - p1->x;
	float l_r = length / sqrtf(dx * dx + dy * dy);
	
	dx *= l_r;
	dy *= l_r;		
	
	*out_dx = dx;
	*out_dy = dy;
}

typedef struct sdl_path {
	SDL_FPoint*	draw_points;
	uint32_t 	n_draw_points;
	SDL_Color 	color;
	int 		closed;
	
	// for filled paths
	SDL_Vertex*	vertices;
	int*		indices;
	uint32_t 	n_indices;	
} sdl_path_t;

void sdl_path_free(sdl_path_t **path) {
	if(path && *path) {
		
		if( (*path)->draw_points ) {
			free((*path)->draw_points);
			(*path)->draw_points = NULL;
			(*path)->n_draw_points = 0;
		}
		
		if( (*path)->vertices ) {
			free((*path)->vertices);
			(*path)->vertices = NULL;
		}
		
		if( (*path)->indices ) {
			free((*path)->indices);
			(*path)->indices = NULL;
			(*path)->n_indices = 0;
		}
		
		free(*path);
		*path = NULL;
	}
}

sdl_path_t *sdl_path_new(SDL_Color *color, float width, int filled, const SDL_FPoint *path_points, int count) {
	
	CHECK_PARAMETER(width >= 1.0f, return NULL);	
	CHECK_PARAMETER(path_points != NULL, return NULL);	
	CHECK_PARAMETER(count >= 2, return NULL); // a path requires at least two points
	
	// allocate struct
	sdl_path_t *path = malloc(sizeof(sdl_path_t));
	CHECK_ALLOC(path, return NULL);
	memset(path, 0, sizeof(*path));
	
	path->color = *color;
	
	// the path is closed when the first and last path_points are equal
	path->closed = path_points[0].x == path_points[count-1].x && path_points[0].y == path_points[count-1].y;
	
	// a closed path requires at least three points
	CHECK_PARAMETER(path->closed == 0 || count >= 3, sdl_path_free(&path); return NULL);
	
	// allocate an array of points to draw
	path->n_draw_points = 2 * count;
	path->draw_points = (SDL_FPoint*)malloc(path->n_draw_points * sizeof(SDL_FPoint));
	CHECK_ALLOC(path->draw_points, sdl_path_free(&path); sdl_path_free(&path); return NULL);
	
	// separate inner and outer draw_points into separate arrays
	SDL_FPoint *i_draw_points = path->draw_points;
	SDL_FPoint *o_draw_points = path->draw_points + count;
	
	// compute offset to be applied in both directions
	float offset = 0.5f * width;
	
	float dx, dy;
	const SDL_FPoint *p0, *p1;
	
	// compute draw_points associated with all except the first and last path_points, to be handled later 
	for (int i = 1; i < count-1; i++) {
		p0 = &path_points[i + 1];
		p1 = &path_points[i - 1];
		compute_perpendicular_vector(p0, p1, offset, &dx, &dy);
		
		// compute inner draw_points, iterating forward
		i_draw_points[i].x = path_points[i].x + dx;
		i_draw_points[i].y = path_points[i].y - dy;
		
		// compute outer draw_points, iterating in reverse
		o_draw_points[count-1 - i].x = path_points[i].x - dx;
		o_draw_points[count-1 - i].y = path_points[i].y + dy;
	}
	
	// compute draw_points for the first and last path_points 
	if(path->closed) {
		
		// compute vector associated with first and last path_points
		p0 = &path_points[1];
		p1 = &path_points[count-2];
		compute_perpendicular_vector(p0, p1, offset, &dx, &dy);
		
		// compute inner draw_points associated with first and last path_points
		i_draw_points[0].x = i_draw_points[count-1].x = path_points[0].x + dx;
		i_draw_points[0].y = i_draw_points[count-1].y = path_points[0].y - dy;
		
		// compute outer draw_points associated with first and last path_points
		o_draw_points[count-1].x = o_draw_points[0].x = path_points[0].x - dx;
		o_draw_points[count-1].y = o_draw_points[0].y = path_points[0].y + dy;
		
	} else {
		
		// compute vector associated with first path_point
		p0 = &path_points[1];
		p1 = &path_points[0];
		compute_perpendicular_vector(p0, p1, offset, &dx, &dy);
		
		// compute inner draw_point associated with first path_point
		i_draw_points[0].x = path_points[0].x + dx;
		i_draw_points[0].y = path_points[0].y - dy;
		
		// compute outer draw_point associated with first path_point
		o_draw_points[count-1].x = path_points[0].x - dx;
		o_draw_points[count-1].y = path_points[0].y + dy;
		
		
		// compute vector associated with last path_point
		p0 = &path_points[count-1];
		p1 = &path_points[count-2];
		compute_perpendicular_vector(p0, p1, offset, &dx, &dy);
		
		// compute inner draw_point associated with last path_point
		i_draw_points[count-1].x = path_points[count-1].x + dx;
		i_draw_points[count-1].y = path_points[count-1].y - dy;
		
		// compute outer draw_point associated with last path_point
		o_draw_points[0].x = path_points[count-1].x - dx;
		o_draw_points[0].y = path_points[count-1].y + dy;
		
	}
	
	
	if (filled) {
		
		// convert draw_points to vertices for triangulation 
		path->vertices = (SDL_Vertex*) malloc(path->n_draw_points * sizeof(SDL_Vertex));
		CHECK_ALLOC(path->vertices,
					sdl_path_free(&path);
					return NULL);
		
		for (uint32_t i = 0; i < path->n_draw_points; i++) {
			path->vertices[i].position = path->draw_points[i];
			path->vertices[i].color = path->color;
		}
		
		// compute indices for rendering triangles
		path->n_indices = (path->n_draw_points - 2) * 3;
		path->indices = (int*) malloc(path->n_indices * sizeof(int));
		CHECK_ALLOC(path->indices,
					sdl_path_free(&path);
					return NULL);
		
		for(int i = 0, *idx = path->indices; i < count-1; i++, idx+= 6) {
			idx[0] = i;
			idx[1] = (path->n_draw_points - 1) - i;
			idx[2] = i + 1;
			
			idx[3] = (path->n_draw_points - 1) - i;
			idx[4] = i + 1;
			idx[5] = (path->n_draw_points - 1) - (i + 1);
		}		
	}
	
	return path;
}

sdl_path_t *sdl_path_new_from_frect(SDL_Color *color, float width, int filled, SDL_FRect rect) {
	SDL_FPoint path_points[5] = {
		{ rect.x, rect.y },
		{ rect.x + rect.w, rect.y },
		{ rect.x + rect.w, rect.y + rect.h },
		{ rect.x, rect.y + rect.h },
		{ rect.x, rect.y }, // closed path
	};
	
	return sdl_path_new(color, width, filled, path_points, 5);
}

sdl_path_t *sdl_path_new_from_rect(SDL_Color *color, float width, int filled, SDL_Rect rect) {
	SDL_FPoint path_points[5] = {
		{ rect.x, rect.y },
		{ rect.x + rect.w, rect.y },
		{ rect.x + rect.w, rect.y + rect.h },
		{ rect.x, rect.y + rect.h },
		{ rect.x, rect.y }, // closed path
	};
	
	return sdl_path_new(color, width, filled, path_points, 5);
}

int sdl_path_render(sdl_path_t *path, SDL_Renderer *renderer) {
	CHECK_PARAMETER(path != NULL, return -1);
	CHECK_PARAMETER(renderer != NULL, return -1);
	
	if(path->vertices) {
		// render filled path
		SDL_RenderGeometry(renderer, NULL, path->vertices, path->n_draw_points, path->indices, path->n_indices);
	} else {
		
		// set color for lines
		SDL_SetRenderDrawColor(renderer, path->color.r, path->color.g, path->color.b, path->color.a);
		
		if(path->closed) {
			// draw inner closed polygon
			SDL_RenderDrawLinesF(renderer, path->draw_points, path->n_draw_points/2);
			
			// draw outer closed polygon
			SDL_RenderDrawLinesF(renderer, path->draw_points + (path->n_draw_points/2), path->n_draw_points/2);
		} else {			
			// draw open polygon
			SDL_RenderDrawLinesF(renderer, path->draw_points, path->n_draw_points);
			
			// close polygon
			SDL_RenderDrawLineF(renderer, path->draw_points[path->n_draw_points - 1].x, path->draw_points[path->n_draw_points - 1].y, path->draw_points[0].x, path->draw_points[0].y);			
		}
	}
	
	return 0;
}

// -- text

int draw_sdl_render_text_at_position(SDL_Renderer *renderer, TTF_Font *font, const char *text, anchor_position_t anchor, SDL_Color color, int x, int y) {
	CHECK_PARAMETER(renderer != NULL, return -1);
	CHECK_PARAMETER(font != NULL, return -1);
	CHECK_PARAMETER(text != NULL, return -1);
	
	// create the text surface
	SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
	CHECK_ERROR(surface != NULL, return -1, "failed to create surface: %s", TTF_GetError());
	
	// create the texture from the surface
	SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_FreeSurface(surface);
	CHECK_ERROR(texture != NULL, return -1, "failed to create texture: %s", SDL_GetError());
	
	// blit the texture to the renderer at the specified position with the given anchor
	draw_sdl_blit_texture(renderer, texture, (float)x, (float)y, anchor);
	
	// destroy the texture
	SDL_DestroyTexture(texture);
	
	return 0;
}

int draw_sdl_render_text(SDL_Renderer *renderer, SDL_Texture **texture, TTF_Font *font, SDL_Color *color, const char *text, int *out_width, int *out_height) {		
	CHECK_PARAMETER(renderer != NULL, return -1);
	CHECK_PARAMETER(texture != NULL, return -1);	
	CHECK_PARAMETER(font != NULL, return -1);
	CHECK_PARAMETER(color != NULL, return -1);
	CHECK_PARAMETER(text != NULL, return -1);
	
	SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, *color);
	CHECK_ERROR(surface != NULL, return -1, "failed to create surface: %s", TTF_GetError());
	
	if(*texture) SDL_DestroyTexture(*texture);
	
	*texture = SDL_CreateTextureFromSurface(renderer, surface);
	CHECK_ERROR(texture != NULL, SDL_FreeSurface(surface); return -1, "failed to create texture: %s", SDL_GetError());
	
	if(out_width) *out_width = surface->w;
	if(out_height) *out_height = surface->h;
	SDL_FreeSurface(surface);
	
	return 0;
}

// -- polygons

// frees the memory allocated for an sdl_polygon_t
void sdl_polygon_free(sdl_polygon_t **poly) {
	if (poly == NULL || *poly == NULL) {
		return;
	}
	
	sdl_polygon_t *polygon = *poly;
	
	// free the vertices array
	if (polygon->vertices) {
		free(polygon->vertices);
	}
	
	// free the indices array
	if (polygon->indices) {
		free(polygon->indices);
	}
	
	// free the polygon struct itself
	free(polygon);
	
	// set the pointer to NULL to avoid dangling pointers
	*poly = NULL;
}

void sdl_polygon_offset(sdl_polygon_t *poly, float x, float y) {
	if (poly == NULL || poly->vertices == NULL) {
		return;
	}
	
	for (uint32_t i = 0; i < poly->num_vertices; i++) {
		poly->vertices[i].position.x += x;
		poly->vertices[i].position.y += y;
	}
}

// creates a texture from an sdl_polygon_t
SDL_Texture* sdl_polygon_render_to_texture(SDL_Renderer *renderer, sdl_polygon_t *poly) {
	if (poly == NULL || renderer == NULL) {
		return NULL;
	}
	
	// determine the bounding box of the polygon
	float min_x = poly->vertices[0].position.x;
	float min_y = poly->vertices[0].position.y;
	float max_x = poly->vertices[0].position.x;
	float max_y = poly->vertices[0].position.y;
	
	for (uint32_t i = 1; i < poly->num_vertices; i++) {
		if (poly->vertices[i].position.x < min_x) {
			min_x = poly->vertices[i].position.x;
		}
		if (poly->vertices[i].position.x > max_x) {
			max_x = poly->vertices[i].position.x;
		}
		if (poly->vertices[i].position.y < min_y) {
			min_y = poly->vertices[i].position.y;
		}
		if (poly->vertices[i].position.y > max_y) {
			max_y = poly->vertices[i].position.y;
		}
	}
	
	// calculate width and height of the texture
	int width = (int)(max_x - min_x) + 1;
	int height = (int)(max_y - min_y) + 1;
	
	// create a transparent texture
	SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
	if (texture == NULL) {
		return NULL;
	}
	
	// set the blend mode for the texture to enable transparency
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	
	// set the rendering target to the texture
	SDL_SetRenderTarget(renderer, texture);
	
	// clear the texture with a transparent color
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);
	
	// offset vertices to fit within the texture
	draw_sdl_offset_vertices(poly->vertices, poly->num_vertices, -min_x, -min_y);
	
	// render the polygon to the texture
	SDL_RenderGeometry(renderer, NULL, poly->vertices, poly->num_vertices, poly->indices, poly->num_indices);
	
	// reset the rendering target
	SDL_SetRenderTarget(renderer, NULL);
	
	// restore the original vertex positions
	draw_sdl_offset_vertices(poly->vertices, poly->num_vertices, min_x, min_y);
	
	return texture;
}

// creates polygon indices for rendering a convex polygon
static int* draw_sdl_create_polygon_indices(SDL_Vertex *vertices, uint32_t n_vertices, uint32_t *out_n_indices) {
	// ensure there are enough vertices to form a polygon
	if (n_vertices < 3) {
		*out_n_indices = 0;
		return NULL;
	}
	
	// number of triangles in a convex polygon is (n_vertices - 2)
	uint32_t n_indices = (n_vertices - 2) * 3;
	
	int* indices = (int*)malloc(sizeof(int) * n_indices);
	CHECK_ALLOC(indices, return NULL);
	
	// create indices for a triangle fan
	uint32_t index = 0;
	for (uint32_t i = 1; i < n_vertices - 1; i++) {
		indices[index++] = 0;        // center vertex of the fan
		indices[index++] = i;        // current vertex
		indices[index++] = i + 1;    // next vertex
	}
	
	*out_n_indices = n_indices;
	return indices;
}

static SDL_Vertex* draw_sdl_create_circle_vertices(float r, uint32_t *out_n, SDL_Color color) {
	// calculate the number of segments needed for a smooth circle
	uint32_t n = (uint32_t)(2.0f * M_PI * r);
	
	// ensure we have at least 12 segments for very small circles
	if (n < 12) {
		n = 12;
	}
	
	SDL_Vertex* vertices = (SDL_Vertex*)malloc(sizeof(SDL_Vertex) * n);
	CHECK_ALLOC(vertices, return NULL);
	
	float angle_increment = 2.0f * M_PI / (float)n;
	for (uint32_t i = 0; i < n; i++) {
		float angle = angle_increment * (float)i;
		vertices[i].position.x = r * cosf(angle);
		vertices[i].position.y = r * sinf(angle);
		vertices[i].color = color;
		vertices[i].tex_coord.x = 0.0f;
		vertices[i].tex_coord.y = 0.0f;
	}
	
	*out_n = n;
	return vertices;
}

sdl_polygon_t* circle_new(float r, SDL_Color *color) {
	sdl_polygon_t* polygon = (sdl_polygon_t*)malloc(sizeof(sdl_polygon_t));
	CHECK_ALLOC(polygon, return NULL);
	
	polygon->vertices = draw_sdl_create_circle_vertices(r, &polygon->num_vertices, *color);
	CHECK_ALLOC(polygon->vertices, free(polygon); return NULL);
	
	polygon->indices = draw_sdl_create_polygon_indices(polygon->vertices, polygon->num_vertices, &polygon->num_indices);
	CHECK_ALLOC(polygon->indices, free(polygon->vertices); free(polygon); return NULL);
	
	return polygon;
}

sdl_polygon_t* frame_new(float x, float y, float width, float height, float edge_width, SDL_Color color) {
	
	// center frame on defined border 
	x -= edge_width * 0.5f;
	y -= edge_width * 0.5f;
	width += edge_width;
	height += edge_width;
	
	// calculate the inner rectangle dimensions
	float inner_x = x + edge_width;
	float inner_y = y + edge_width;
	float inner_width = width - 2 * edge_width;
	float inner_height = height - 2 * edge_width;

	// ensure the inner rectangle is valid
	if (inner_width <= 0 || inner_height <= 0) {
		return NULL;
	}

	// allocate memory for the vertices
	SDL_Vertex* vertices = (SDL_Vertex*)malloc(sizeof(SDL_Vertex) * 8);
	CHECK_ALLOC(vertices, return NULL);

	// set up the vertices for the outer and inner rectangles
	vertices[0] = (SDL_Vertex){{x, y}, color, {0.0f, 0.0f}};                       // top-left outer
	vertices[1] = (SDL_Vertex){{x + width, y}, color, {0.0f, 0.0f}};               // top-right outer
	vertices[2] = (SDL_Vertex){{x + width, y + height}, color, {0.0f, 0.0f}};      // bottom-right outer
	vertices[3] = (SDL_Vertex){{x, y + height}, color, {0.0f, 0.0f}};              // bottom-left outer
	vertices[4] = (SDL_Vertex){{inner_x, inner_y}, color, {0.0f, 0.0f}};           // top-left inner
	vertices[5] = (SDL_Vertex){{inner_x + inner_width, inner_y}, color, {0.0f, 0.0f}};  // top-right inner
	vertices[6] = (SDL_Vertex){{inner_x + inner_width, inner_y + inner_height}, color, {0.0f, 0.0f}};  // bottom-right inner
	vertices[7] = (SDL_Vertex){{inner_x, inner_y + inner_height}, color, {0.0f, 0.0f}};  // bottom-left inner

	// allocate memory for the indices
	int* indices = (int*)malloc(sizeof(int) * 24);
	CHECK_ALLOC(indices, free(vertices); return NULL);

	// define the triangles for the frame (8 triangles)
	int idx[] = {
		0, 1, 4,  1, 5, 4,  // top-left
		1, 2, 5,  2, 6, 5,  // top-right
		2, 3, 6,  3, 7, 6,  // bottom-right
		3, 0, 7,  0, 4, 7   // bottom-left
	};

	for (int i = 0; i < 24; ++i) {
		indices[i] = idx[i];
	}

	// create the sdl_polygon_t
	sdl_polygon_t* poly = (sdl_polygon_t*)malloc(sizeof(sdl_polygon_t));
	CHECK_ALLOC(poly, free(vertices); free(indices); return NULL);

	poly->vertices = vertices;
	poly->num_vertices = 8;
	poly->indices = indices;
	poly->num_indices = 24;

	return poly;
}


// -- geometry

SDL_Rect draw_sdl_inset_rect(SDL_Rect rect, float inset) {
	
	SDL_Rect new_rect = rect;
	
	new_rect.x += inset;
	new_rect.y += inset;
	new_rect.w -= 2 * inset;
	new_rect.h -= 2 * inset;
	
	return new_rect;
}

void draw_sdl_offset_vertices(SDL_Vertex *vertices, uint32_t n, float offset_x, float offset_y) {
	if (vertices == NULL || n == 0) {
		return;
	}
	
	for (uint32_t i = 0; i < n; i++) {
		vertices[i].position.x += offset_x;
		vertices[i].position.y += offset_y;
	}
}

void draw_sdl_offset_points(SDL_FPoint *points, uint32_t n, float offset_x, float offset_y) {
	if (points == NULL || n == 0) {
		return;
	}
	
	for (uint32_t i = 0; i < n; i++) {
		points[i].x += offset_x;
		points[i].y += offset_y;
	}
}
