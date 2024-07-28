#include "draw_sdl.h"
#include "s9.h"
#include "s9_colors.h"

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

int draw_sdl_render_text_at_position(SDL_Renderer *renderer, TTF_Font *font, const char *text, rect_edge_position_t anchor, SDL_Color color, int x, int y) {
	CHECK_PARAMETER(renderer != NULL, return -1);
	CHECK_PARAMETER(font != NULL, return -1);
	CHECK_PARAMETER(text != NULL, return -1);
	
	// create the text surface
	SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
	CHECK_ERROR(surface != NULL, return -1, "failed to create surface: %s", TTF_GetError());
	
	// get the text width and height from the surface
	int text_width = surface->w;
	int text_height = surface->h;
	
	// calculate the position based on the anchor position
	int render_x = x;
	int render_y = y;
	
	switch (anchor) {
		case POSITION_TOP_LEFT:
			break;
		case POSITION_TOP_MIDDLE:
			render_x -= text_width / 2;
			break;
		case POSITION_TOP_RIGHT:
			render_x -= text_width;
			break;
		case POSITION_MIDDLE_LEFT:
			render_y -= text_height / 2;
			break;
		case POSITION_MIDDLE_RIGHT:
			render_x -= text_width;
			render_y -= text_height / 2;
			break;
		case POSITION_BOTTOM_LEFT:
			render_y -= text_height;
			break;
		case POSITION_BOTTOM_MIDDLE:
			render_x -= text_width / 2;
			render_y -= text_height;
			break;
		case POSITION_BOTTOM_RIGHT:
			render_x -= text_width;
			render_y -= text_height;
			break;
	}
	
	// create the texture from the surface
	SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_FreeSurface(surface);
	CHECK_ERROR(texture != NULL, return -1, "failed to create texture: %s", SDL_GetError());
	
	// set up the destination rectangle
	SDL_Rect dst_rect = { render_x, render_y, text_width, text_height };
	
	// render the texture
	int result = SDL_RenderCopy(renderer, texture, NULL, &dst_rect);
	SDL_DestroyTexture(texture);
	CHECK_ERROR(result == 0, return -1, "failed to render texture: %s", SDL_GetError());
	
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

SDL_Rect draw_sdl_inset_rect(SDL_Rect rect, float inset) {
	
	SDL_Rect new_rect = rect;
	
	new_rect.x += inset;
	new_rect.y += inset;
	new_rect.w -= 2 * inset;
	new_rect.h -= 2 * inset;
	
	return new_rect;
}

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
