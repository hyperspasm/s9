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

int draw_sdl_path(SDL_Renderer *renderer, SDL_Color color, float width, int filled, const SDL_FPoint *path_points, int count)
{
	CHECK_PARAMETER(renderer != NULL, return -1);
	CHECK_PARAMETER(width >= 1.0f, return -1);	
	CHECK_PARAMETER(path_points != NULL, return -1);	
	CHECK_PARAMETER(count >= 2, return -1); // a path requires at least two points
	
	// the path is closed when the first and last path_points are equal
	int closed = path_points[0].x == path_points[count-1].x && path_points[0].y == path_points[count-1].y;
	
	// a closed path requires at least three points
	CHECK_PARAMETER(closed == 0 || count >= 3, return -1);
	
	// allocate an array of points to draw
	SDL_FPoint* draw_points = (SDL_FPoint*)malloc(2 * count * sizeof(SDL_FPoint));
	CHECK_ERROR(draw_points != NULL,
				return -1,
				"failed to allocate memory");

	// compute offset to be applied in both directions
	float offset = 0.5f * width;
	
	float dx, dy;
	const SDL_FPoint *p0, *p1;
	
	// compute draw_points associated with all except the first and last path_points, to be handled later 
	for (int i = 1; i < count-1; i++) {
		p0 = &path_points[i + 1];
		p1 = &path_points[i - 1];
		compute_perpendicular_vector(p0, p1, offset, &dx, &dy);
		
		draw_points[i].x = path_points[i].x + dx;
		draw_points[i].y = path_points[i].y - dy;
		
		draw_points[count * 2 - i - 1].x = path_points[i].x - dx;
		draw_points[count * 2 - i - 1].y = path_points[i].y + dy;
	}
	
	// compute draw_points for the first and last path_points 
	if(closed) {
		
		// compute vector associated with first and last path_points
		p0 = &path_points[1];
		p1 = &path_points[count-2];
		compute_perpendicular_vector(p0, p1, offset, &dx, &dy);
		
		// compute inner draw_points associated with first and last path_points
		draw_points[0].x = draw_points[count-1].x = path_points[0].x + dx;
		draw_points[0].y = draw_points[count-1].y = path_points[0].y - dy;
		
		// compute outer draw_points associated with first and last path_points
		draw_points[count * 2 - 0 - 1].x = draw_points[count * 2 - (count-1) - 1].x = path_points[0].x - dx;
		draw_points[count * 2 - 0 - 1].y = draw_points[count * 2 - (count-1) - 1].y = path_points[0].y + dy;
		
	} else {

		// compute vector associated with first path_point
		p0 = &path_points[1];
		p1 = &path_points[0];
		compute_perpendicular_vector(p0, p1, offset, &dx, &dy);
		
		// compute inner draw_point associated with first path_point
		draw_points[0].x = path_points[0].x + dx;
		draw_points[0].y = path_points[0].y - dy;
		
		// compute outer draw_point associated with first path_point
		draw_points[count * 2 - 0 - 1].x = path_points[0].x - dx;
		draw_points[count * 2 - 0 - 1].y = path_points[0].y + dy;
		
				
		// compute vector associated with list path_point
		p0 = &path_points[count-1];
		p1 = &path_points[count-2];
		compute_perpendicular_vector(p0, p1, offset, &dx, &dy);
		
		// compute inner draw_point associated with last path_point
		draw_points[count-1].x = path_points[count-1].x + dx;
		draw_points[count-1].y = path_points[count-1].y - dy;
		
		// compute outer draw_point associated with last path_point
		draw_points[count * 2 - (count-1) - 1].x = path_points[count-1].x - dx;
		draw_points[count * 2 - (count-1) - 1].y = path_points[count-1].y + dy;
		
	}
	

	if (filled) {
		
		// convert draw_points to vertices for triangulation
		int n_vertices = count *  2; 
		SDL_Vertex* vertices = (SDL_Vertex*) malloc(n_vertices * sizeof(SDL_Vertex));
		CHECK_ERROR(vertices != NULL,
					free(draw_points);
					return -1,
					"failed to allocate memory");
		
		for (int i = 0; i < n_vertices; i++) {
			vertices[i].position = draw_points[i];
			vertices[i].color = color;
		}

		// compute indices for rendering triangles
		int n_indices = (n_vertices - 2) * 3;
		int *indices = (int*) malloc(n_indices * sizeof(int));
		CHECK_ERROR(indices != NULL,
					free(vertices);
					free(draw_points);
					return -1,
					"failed to allocate memory");
		
		for(int i = 0, *idx = indices; i < count-1; i++, idx+= 6) {
			idx[0] = i;
			idx[1] = (n_vertices - 1) - i;
			idx[2] = i + 1;
			
			idx[3] = (n_vertices - 1) - i;
			idx[4] = i + 1;
			idx[5] = (n_vertices - 1) - (i + 1);
		}

		SDL_RenderGeometry(renderer, NULL, vertices, n_vertices, indices, n_indices);

		free(indices);
		free(vertices);
		
	} else {
		
		// set color for lines
		SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
		
		if(closed) {
			// draw inner closed polygon
			SDL_RenderDrawLinesF(renderer, draw_points, count);
			
			// draw outer closed polygon
			SDL_RenderDrawLinesF(renderer, draw_points + count, count);
		} else {			
			// draw open polygon
			SDL_RenderDrawLinesF(renderer, draw_points, 2 * count);
			
			// close polygon
			SDL_RenderDrawLineF(renderer, draw_points[2 * count - 1].x, draw_points[2 * count - 1].y, draw_points[0].x, draw_points[0].y);			
		}		
	}

	free(draw_points);
	
	return 0;
}
