#include "draw_sdl.h"
#include "s9.h"

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
	
	// compute offset to be applied in both directions
	float offset = 0.5f * width;
	
	float dx, dy;
	const SDL_FPoint *p0, *p1;
	
	// compute draw_points associated with all except the first and last path_points, to be handled later 
	for (int i = 1; i < count-1; i++) {
		p0 = &path_points[i + 1];
		p1 = &path_points[i - 1];
		compute_perpendicular_vector(p0, p1, offset, &dx, &dy);
		
		path->draw_points[i].x = path_points[i].x + dx;
		path->draw_points[i].y = path_points[i].y - dy;
		
		path->draw_points[count * 2 - i - 1].x = path_points[i].x - dx;
		path->draw_points[count * 2 - i - 1].y = path_points[i].y + dy;
	}
	
	// compute draw_points for the first and last path_points 
	if(path->closed) {
		
		// compute vector associated with first and last path_points
		p0 = &path_points[1];
		p1 = &path_points[count-2];
		compute_perpendicular_vector(p0, p1, offset, &dx, &dy);
		
		// compute inner draw_points associated with first and last path_points
		path->draw_points[0].x = path->draw_points[count-1].x = path_points[0].x + dx;
		path->draw_points[0].y = path->draw_points[count-1].y = path_points[0].y - dy;
		
		// compute outer draw_points associated with first and last path_points
		path->draw_points[count * 2 - 0 - 1].x = path->draw_points[count * 2 - (count-1) - 1].x = path_points[0].x - dx;
		path->draw_points[count * 2 - 0 - 1].y = path->draw_points[count * 2 - (count-1) - 1].y = path_points[0].y + dy;
		
	} else {
		
		// compute vector associated with first path_point
		p0 = &path_points[1];
		p1 = &path_points[0];
		compute_perpendicular_vector(p0, p1, offset, &dx, &dy);
		
		// compute inner draw_point associated with first path_point
		path->draw_points[0].x = path_points[0].x + dx;
		path->draw_points[0].y = path_points[0].y - dy;
		
		// compute outer draw_point associated with first path_point
		path->draw_points[count * 2 - 0 - 1].x = path_points[0].x - dx;
		path->draw_points[count * 2 - 0 - 1].y = path_points[0].y + dy;
		
		
		// compute vector associated with list path_point
		p0 = &path_points[count-1];
		p1 = &path_points[count-2];
		compute_perpendicular_vector(p0, p1, offset, &dx, &dy);
		
		// compute inner draw_point associated with last path_point
		path->draw_points[count-1].x = path_points[count-1].x + dx;
		path->draw_points[count-1].y = path_points[count-1].y - dy;
		
		// compute outer draw_point associated with last path_point
		path->draw_points[count * 2 - (count-1) - 1].x = path_points[count-1].x - dx;
		path->draw_points[count * 2 - (count-1) - 1].y = path_points[count-1].y + dy;
		
	}
	
	
	if (filled) {
		
		// convert draw_points to vertices for triangulation 
		path->vertices = (SDL_Vertex*) malloc(path->n_draw_points * sizeof(SDL_Vertex));
		CHECK_ALLOC(path->vertices,
					sdl_path_free(&path);
					return NULL);
		
		for (int i = 0; i < path->n_draw_points; i++) {
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
