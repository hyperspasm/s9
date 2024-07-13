#include "s9_app.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

typedef struct s9_app {
	s9_app_config_t config;
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

// --

int s9_app_create(const s9_app_config_t *config)
{
	CHECK_ERROR(g_app == NULL, return -1, "ignoring attempt to create a second app instance");
	
	g_app = malloc(sizeof(s9_app_t));
	if(g_app == NULL) return -1;
	
	g_app->config = *config;	
	
	s9_log(LOG_LEVEL_TRACE, "s9 app created\n");
	return 0;
}

void s9_app_destroy(void)
{
	CHECK_ERROR(g_app != NULL, return, "ignoring attempt destroy uncreated app");
	
	// TODO...
	
	free(g_app);
	g_app = NULL;
}

int s9_app_run(void)
{
	CHECK_ERROR(g_app != NULL, return -1, "app not created");
	
	// TODO...
	
	return 1;
}
