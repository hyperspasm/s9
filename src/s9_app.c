#include "s9_app.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

typedef struct s9_app {
	s9_app_config_t config;	
	SDL_Window *window;
	SDL_Renderer *renderer;
	
	int ret;
} s9_app_t;

static s9_app_t *g_app = NULL; // singleton instance

// -- logging

#define PRINT_BUFFER_SIZE 1024

static void default_log_func(const char *text)
{
	fprintf(stderr, "%s", text);
}

static void default_print_func(const char *text)
{
	fprintf(stdout, "%s", text);
}

int s9_log(s9_log_level_t log_level, const char *format, ...)
{
	int ret;
	va_list args;
	char buffer[PRINT_BUFFER_SIZE];
	void (*print_func)(const char *text) = default_log_func;
	
	if(g_app != NULL && g_app->config.log_line != NULL) print_func = g_app->config.log_line;
	
	if (log_level < LOG_LEVEL_TRACE || log_level > LOG_LEVEL_CRITICAL) {
		return -1;
	}
	
	va_start(args, format);
	ret = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	
	if(ret > 0)
	{
		static const char *prefixes[] = {
			"TRACE: ",
			"DEBUG: ",
			"INFO: ",
			"WARNING: ",
			"ERROR: ",
			"CRITICAL: ",
		};
		
		print_func(prefixes[log_level]);		
		print_func(buffer);
		print_func("\n");
	}
	
	return ret;
}

int s9_print(const char *format, ...)
{
	int ret;
	va_list args;
	char buffer[PRINT_BUFFER_SIZE];
	void (*print_func)(const char *text) = default_print_func;
	
	if(g_app != NULL && g_app->config.console_print != NULL) print_func = g_app->config.console_print;	
	
	va_start(args, format);
	ret = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	
	if(ret > 0)
	{
		print_func(buffer);
	}
	
	return ret;
}

// -- private

static void print_renderer_info(SDL_Window *window, SDL_Renderer *renderer) {
	SDL_RendererInfo info;
	CHECK_ERROR(SDL_GetRendererInfo(renderer, &info) == 0,
				return,
				"SDL_GetRendererInfo failed: %s", SDL_GetError());
	
	s9_log(LOG_LEVEL_INFO, "Renderer: %s", info.name);
	if (info.flags & SDL_RENDERER_SOFTWARE) s9_log(LOG_LEVEL_INFO, "    * Software");
	if (info.flags & SDL_RENDERER_ACCELERATED) s9_log(LOG_LEVEL_INFO, "    * Accelerated");
	if (info.flags & SDL_RENDERER_PRESENTVSYNC) s9_log(LOG_LEVEL_INFO, "    * VSync");
	if (info.flags & SDL_RENDERER_TARGETTEXTURE) s9_log(LOG_LEVEL_INFO, "    * Render to texture");
	s9_log(LOG_LEVEL_INFO, "    Max texture %d x %d", info.max_texture_width, info.max_texture_height);
	
	int display_index = SDL_GetWindowDisplayIndex(window);
	CHECK_ERROR(display_index >= 0,
				return,
				"SDL_GetWindowDisplayIndex failed: %s", SDL_GetError());
	
	SDL_DisplayMode display_mode;
	CHECK_ERROR(SDL_GetCurrentDisplayMode(display_index, &display_mode) == 0,
				return,
				"SDL_GetCurrentDisplayMode failed: %s", SDL_GetError());
	
	s9_log(LOG_LEVEL_INFO, "Display mode:");
	s9_log(LOG_LEVEL_INFO, "    %s", SDL_GetPixelFormatName(display_mode.format));
	s9_log(LOG_LEVEL_INFO, "    %d x %d", display_mode.w, display_mode.h);
	s9_log(LOG_LEVEL_INFO, "    %d Hz", display_mode.refresh_rate);
}

// --

int s9_app_create(const s9_app_config_t *config)
{
	int ret;
	CHECK_ERROR(g_app == NULL, return -1, "ignoring attempt to create a second app instance");
	
	g_app = malloc(sizeof(s9_app_t));
	CHECK_ERROR(g_app != NULL, return -1, "failed to allocate app memory");	
	memset(g_app, 0, sizeof(*g_app));
	
	g_app->config = *config;	
	
	// initialize SDL
	ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO); 
	CHECK_ERROR(ret == 0,
				free(g_app);
				g_app = NULL;
				return -1,
				"SDL_Init failed: %s", SDL_GetError());
	
	// initialize SDL_ttf
	ret = TTF_Init();
	CHECK_ERROR(ret == 0,
				free(g_app);
				g_app = NULL;
				SDL_Quit();
				return -1,
				"TTF_Init failed: %d", ret);
	
	// create window
	// on macOS NSHighResolutionCapable must be set true in the application's Info.plist for SDL_WINDOW_ALLOW_HIGHDPI to have any effect.
	g_app->window = SDL_CreateWindow("S9",
									 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
									 g_app->config.window_width, g_app->config.window_height,
									 SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);	
	CHECK_ERROR(g_app->window != NULL,
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"SDL_CreateWindow failed: %s", SDL_GetError());
	
	// create renderer
	g_app->renderer = SDL_CreateRenderer(g_app->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	CHECK_ERROR(g_app->renderer != NULL,
				SDL_DestroyWindow(g_app->window);
				free(g_app);
				g_app = NULL;
				TTF_Quit();
				SDL_Quit();
				return -1,
				"SDL_CreateRenderer failed: %s", SDL_GetError());
	
	print_renderer_info(g_app->window, g_app->renderer);
	
	return 0;
}

void s9_app_destroy(void)
{
	CHECK_ERROR(g_app != NULL, return, "ignoring attempt destroy uncreated app");
	
	TTF_Quit();
	SDL_Quit();
	
	free(g_app);
	g_app = NULL;
}

int s9_app_run(void)
{
	SDL_Event e;
	
	CHECK_ERROR(g_app != NULL, return -1, "app not created");
	
	// exit if necessary
	if (g_app->ret) return g_app->ret;
	
	// poll until all events are handled!
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT) {
			g_app->ret = 1;
		} else if (e.type == SDL_KEYDOWN) {
			s9_log(LOG_LEVEL_TRACE, "key pressed: %s\n", SDL_GetKeyName(e.key.keysym.sym));
		}		
	}
	
	// exit if necessary
	if (g_app->ret) return g_app->ret;	
	
	// clear screen
	SDL_SetRenderDrawColor(g_app->renderer, 0, 0, 0, 255);
	SDL_RenderClear(g_app->renderer);
	
	// present renderer
	SDL_RenderPresent(g_app->renderer);
	
	return 0;
}
