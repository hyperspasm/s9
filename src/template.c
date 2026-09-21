#include "template.h"
#include "s9.h"
#include <stdlib.h>

typedef struct template {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	
	// TODO
	
} template_t;

template_t *template_new(void) {
	template_t *template = (template_t *) malloc(sizeof(template_t));
	CHECK_ALLOC(template, return NULL);
	memset(template, 0, sizeof(*template));
	
	// TODO
	
	return template;
}

void template_free(template_t **tp) {
	if (tp && *tp) {
		
		// TODO
		
		free(*tp);
		*tp = NULL;
	}
}

static S9_FORCE_INLINE void prepare_to_draw(template_t *tp, SDL_Renderer *renderer) {
	SDL_Rect viewport;	
	SDL_GetRenderViewport(renderer, &viewport);
	
	if(tp->renderer != renderer || !SDL_RectsEqual(&tp->viewport, &viewport)) {
		tp->renderer = renderer;
		tp->viewport = viewport;		
		
		// TODO
	}
}

void template_render(template_t *template, SDL_Renderer *renderer) {
	prepare_to_draw(template, renderer);
	
	// TODO
}
