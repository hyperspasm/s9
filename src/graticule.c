#include "graticule.h"
#include "s9.h"
#include "draw_sdl.h"

typedef struct graticule {
	SDL_Renderer *renderer;
	SDL_Rect rect;

	graticule_config_t config;

	SDL_Texture *texture;
} graticule_t;

void graticule_set_config(graticule_t *grat, const graticule_config_t *config) {
	if (config) {
		grat->config = *config;
	} else {
		grat->config = default_graticule_config;
	}
	grat->renderer = NULL;
}

graticule_t *graticule_new(const graticule_config_t *config) {
	graticule_t *grat = malloc(sizeof(graticule_t));
	CHECK_ALLOC(grat, return NULL);
	memset(grat, 0, sizeof(*grat));
	graticule_set_config(grat, config);

	return grat;
}

void graticule_free(graticule_t **grat) {
	if (grat && *grat) {
		if ((*grat)->texture) {
			SDL_DestroyTexture((*grat)->texture);
			(*grat)->texture = NULL;
		}

		free(*grat);
		*grat = NULL;
	}
}

static void draw_graticule(graticule_t *grat, SDL_Renderer *renderer) {
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);

	const SDL_Rect *rect = &grat->rect;
	float major_line_width = 1.0f;

	// draw minor vertical lines (1/10, 2/10, 3/10, 4/10, 6/10, 7/10, 8/10, 9/10)
	SDL_SET_RENDER_DRAW_COLOR(renderer, &grat->config.minor_color);
	for (int i = 1; i <= 9; i++) {
		if (i == 5) continue;  // skip center (major line)

		float x = rect->x + (rect->w * i / 10.0f);
		SDL_RenderDrawLineF(renderer,
			x, rect->y,
			x, rect->y + rect->h);
	}

	// draw minor horizontal lines
	for (int i = 1; i <= 9; i++) {
		if (i == 5) continue;  // skip center (major line)

		float y = rect->y + (rect->h * i / 10.0f);
		SDL_RenderDrawLineF(renderer,
			rect->x, y,
			rect->x + rect->w, y);
	}

	// draw major vertical center line
	{
		float x = rect->x + (rect->w * 0.5f);
		sdl_polygon_t *line = rect_new(
			x - major_line_width * 0.5f,
			rect->y - major_line_width * 0.5f,
			major_line_width,
			rect->h + major_line_width,
			grat->config.major_color);
		if (line) {
			SDL_RENDER_GEOMETRY_POLYGON(renderer, line);
			sdl_polygon_free(&line);
		}
	}

	// draw major horizontal center line
	{
		float y = rect->y + (rect->h * 0.5f);
		sdl_polygon_t *line = rect_new(
			rect->x - major_line_width * 0.5f,
			y - major_line_width * 0.5f,
			rect->w + major_line_width,
			major_line_width,
			grat->config.major_color);
		if (line) {
			SDL_RENDER_GEOMETRY_POLYGON(renderer, line);
			sdl_polygon_free(&line);
		}
	}

	// draw frame
	sdl_polygon_t *frame = frame_new_from_rect(*rect, major_line_width, grat->config.major_color);
	if (frame) {
		SDL_RENDER_GEOMETRY_POLYGON(renderer, frame);
		sdl_polygon_free(&frame);
	}
}

static void prepare_to_draw(graticule_t *grat, SDL_Renderer *renderer, const SDL_Rect *rect) {
	SDL_Rect viewport;
	SDL_RenderGetViewport(renderer, &viewport);

	if (grat->renderer != renderer || !SDL_RectEquals(&grat->rect, rect)) {
		grat->renderer = renderer;
		grat->rect = *rect;

		if (grat->texture) {
			SDL_DestroyTexture(grat->texture);
			grat->texture = NULL;
		}

		grat->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, viewport.w, viewport.h);
		CHECK_ALLOC(grat->texture, return);

		int ret = SDL_SetRenderTarget(renderer, grat->texture);
		CHECK_ERROR(ret == 0,
			SDL_DestroyTexture(grat->texture);
			grat->texture = NULL;
			return,
			"could not set render target");

		ret = SDL_SetTextureBlendMode(grat->texture, SDL_BLENDMODE_BLEND);
		CHECK_ERROR(ret == 0,
			SDL_DestroyTexture(grat->texture);
			grat->texture = NULL;
			return,
			"could not set blend mode");

		draw_graticule(grat, renderer);

		SDL_SetRenderTarget(renderer, NULL);
	}
}

void graticule_render(graticule_t *grat, SDL_Renderer *renderer, const SDL_Rect *rect) {
	prepare_to_draw(grat, renderer, rect);

	if (grat->texture) {
		SDL_Rect viewport;
		SDL_RenderGetViewport(renderer, &viewport);
		SDL_Rect dst_rect = { 0, 0, viewport.w, viewport.h };
		SDL_RenderCopy(renderer, grat->texture, NULL, &dst_rect);
	}
}
