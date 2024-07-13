#ifndef s9_app_h
#define s9_app_h

#include "s9.h"

typedef struct {
	// function that prints a line to logs (CR/LF automatically appended)
	void (*log_line)(const char *text);
	s9_log_level_t log_level;
	
	// function that prints some text to the console
	void (*console_print)(const char *text);
	
	uint16_t window_width;
	uint16_t window_height;
	
} s9_app_config_t;

int s9_app_create(const s9_app_config_t *config);
void s9_app_destroy(void);

// call this function repeatedly until it returns non-zero
int s9_app_run(void);

#endif /* s9_app_h */
