#ifndef s9_app_h
#define s9_app_h

#include "s9.h"
#include "s9_modes.h"

#include <stdbool.h>

typedef struct {
	// function that prints a line to logs (CR/LF automatically appended)
	void (*log_line)(const char *text);
	s9_log_level_t log_level;
	
	// function that prints some text to the console
	void (*console_print)(const char *text);
	
	// window size
	uint16_t window_width;
	uint16_t window_height;
	
	// audio device name
	char *audio_device;
	
	// sample rate
	int sample_rate;

	// TCP port for the parameter server and the web interface
	uint16_t port;

	// directory of the web interface's files
	char *www_dir;

} s9_app_config_t;

int s9_app_create(const s9_app_config_t *config);
void s9_app_destroy(void);

// call this function repeatedly until it returns non-zero
int s9_app_run(void);

// display mode. Setting the current mode again is a no-op; an invalid id fails.
int s9_app_set_mode(s9_mode_id_t mode);
s9_mode_id_t s9_app_get_mode(void);

// frame rate counter, off until shown
void s9_app_set_show_fps(bool show);
bool s9_app_get_show_fps(void);

#endif /* s9_app_h */
