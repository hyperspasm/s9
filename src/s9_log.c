#include "s9.h"
#include <stdio.h>
#include <stdarg.h>

#define PRINT_BUFFER_SIZE 1024

static void default_log_func(const char *text) {
	fprintf(stderr, "%s", text);
}

static void default_print_func(const char *text) {
	fprintf(stdout, "%s", text);
}

static s9_log_level_t s9_log_level = LOG_LEVEL_INFO;
static void (*s9_log_line)(const char *text) = default_log_func;
static void (*s9_console_print)(const char *text) = default_print_func;

void s9_set_log_level(s9_log_level_t log_level) {
	s9_log_level = log_level;
}

void s9_set_logging_function(void (*log_line_func)(const char *text)) {
	s9_log_line = log_line_func;
}

void s9_set_console_print_function(void (*console_print_func)(const char *text)) {
	s9_console_print = console_print_func;
}

int s9_log(s9_log_level_t log_level, const char *format, ...) {
	int ret;
	va_list args;
	char buffer[PRINT_BUFFER_SIZE];
	
	if (log_level < LOG_LEVEL_TRACE || log_level > LOG_LEVEL_CRITICAL) {
		return -1;
	}
	
	va_start(args, format);
	ret = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	
	if(ret > 0) {
		static const char *prefixes[] = {
			"TRACE: ",
			"DEBUG: ",
			"INFO: ",
			"WARNING: ",
			"ERROR: ",
			"CRITICAL: ",
		};
		
		s9_log_line(prefixes[log_level]);        
		s9_log_line(buffer);
		s9_log_line("\n");
	}
	
	return ret;
}

int s9_print(const char *format, ...) {
	int ret;
	va_list args;
	char buffer[PRINT_BUFFER_SIZE];
	
	va_start(args, format);
	ret = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	
	if(ret > 0) {
		s9_console_print(buffer);
	}
	
	return ret;
}
