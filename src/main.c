#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "s9_app.h"

// default configuration values
#define DEFAULT_WINDOW_WIDTH 1280
#define DEFAULT_WINDOW_HEIGHT 720
#define DEFAULT_LOG_LEVEL LOG_LEVEL_TRACE

static void log_line(const char *text) {
	fprintf(stderr, "%s", text);
}

static void console_print(const char *text) {
	fprintf(stdout, "%s", text);
}

// function to load configuration from ~/.s9rc
void load_config_file(s9_app_config_t *config) {
	char *home = getenv("HOME");
	if (home == NULL) {
		return;
	}

	char path[1024];
	snprintf(path, sizeof(path), "%s/.s9rc", home);
	
	FILE *file = fopen(path, "r");
	if (file == NULL) {
		return;
	}

	s9_log(LOG_LEVEL_INFO, "parsing %s", path);

	char line[256];
	while (fgets(line, sizeof(line), file) != NULL) {
		// Ignore comments
		if (line[0] == '#') {
			continue;
		}
		// Parse key-value pairs
		if (sscanf(line, "window_width=%hu", &config->window_width) == 1) {
			continue;
		}
		if (sscanf(line, "window_height=%hu", &config->window_height) == 1) {
			continue;
		}
		if (sscanf(line, "log_level=%d", (int *)&config->log_level) == 1) {
			continue;
		}
		// Parse audio device name
		if (strncmp(line, "audio_device=", 13) == 0) {
			config->audio_device = strdup(line + 13);
			// Remove newline character if present
			config->audio_device[strcspn(config->audio_device, "\n")] = '\0';
		}
	}

	fclose(file);
}

int main(int argc, const char * argv[]) {
	int ret;

	s9_app_config_t config = {
		.log_line = log_line,
		.log_level = DEFAULT_LOG_LEVEL,
		.console_print = console_print,
		.window_width = DEFAULT_WINDOW_WIDTH,
		.window_height = DEFAULT_WINDOW_HEIGHT,
		.audio_device = NULL,
	};

	// load configuration from file
	load_config_file(&config);

	// parse command line arguments
	int opt;
	while ((opt = getopt(argc, (char * const *)argv, "w:h:l:a:")) != -1) {
		switch (opt) {
			case 'w':
				config.window_width = (uint16_t)atoi(optarg);
				break;
			case 'h':
				config.window_height = (uint16_t)atoi(optarg);
				break;
			case 'l':
				config.log_level = (s9_log_level_t)atoi(optarg);
				break;
			case 'a':
				if(config.audio_device) free(config.audio_device); // free previous value if any
				config.audio_device = strdup(optarg);
				break;
			default:
				fprintf(stderr, "Usage: %s [-w window_width] [-h window_height] [-l log_level] [-a audio_device]\n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	ret = s9_app_create(&config);
	while (ret == 0) {
		ret = s9_app_run();
	}

	s9_app_destroy();

	if(config.audio_device) free(config.audio_device);

	return ret >= 0 ? 0 : ret;
}
