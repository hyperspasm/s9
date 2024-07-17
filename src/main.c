#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "s9_app.h"

// default configuration values
#define DEFAULT_WINDOW_WIDTH 1280
#define DEFAULT_WINDOW_HEIGHT 720
#define DEFAULT_LOG_LEVEL LOG_LEVEL_INFO
#define DEFAULT_SAMPLE_RATE 44100

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
	
	S9_LOG_INFO("parsing %s", path);
	
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
		if (sscanf(line, "sample_rate=%d", &config->sample_rate) == 1) {
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

void print_help(const char *prog_name) {
	fprintf(stdout, "Usage: %s [options]\n", prog_name);
	fprintf(stdout, "Options:\n");
	fprintf(stdout, "  -w, --window_width <width>       Set window width (default: %d)\n", DEFAULT_WINDOW_WIDTH);
	fprintf(stdout, "  -h, --window_height <height>     Set window height (default: %d)\n", DEFAULT_WINDOW_HEIGHT);
	fprintf(stdout, "  -l, --log_level <log_level>      Set log level (default: %d)\n", DEFAULT_LOG_LEVEL);
	fprintf(stdout, "  -a, --audio_device <device name> Set audio device\n");
	fprintf(stdout, "  -r, --sample_rate <rate>         Set sample rate (default: %d)\n", DEFAULT_SAMPLE_RATE);
	fprintf(stdout, "  --help                           Display this help message\n");
	fprintf(stdout, "\nConfiguration File:\n");
	fprintf(stdout, "  The application can also be configured using a ~/.s9rc file.\n");
	fprintf(stdout, "  The configuration file supports the following key-value pairs:\n");
	fprintf(stdout, "    window_width=<width>\n");
	fprintf(stdout, "    window_height=<height>\n");
	fprintf(stdout, "    log_level=<log_level>\n");
	fprintf(stdout, "    sample_rate=<rate>\n");
	fprintf(stdout, "    audio_device=<device name>\n");
}

void log_config_options(const s9_app_config_t *config) {
	S9_LOG_INFO("Configuration options:");
	S9_LOG_INFO("  window_width: %hu", config->window_width);
	S9_LOG_INFO("  window_height: %hu", config->window_height);
	S9_LOG_INFO("  log_level: %d", config->log_level);
	S9_LOG_INFO("  sample_rate: %d", config->sample_rate);
	if (config->audio_device) {
		S9_LOG_INFO("  audio_device: %s", config->audio_device);
	} else {
		S9_LOG_INFO("  audio_device: (default)");
	}
}

int main(int argc, const char * argv[]) {
	int ret;
	
	s9_app_config_t config = {
		.log_line = NULL, // default
		.log_level = DEFAULT_LOG_LEVEL,
		.console_print = NULL, // default
		.window_width = DEFAULT_WINDOW_WIDTH,
		.window_height = DEFAULT_WINDOW_HEIGHT,
		.audio_device = NULL,
		.sample_rate = DEFAULT_SAMPLE_RATE,
	};
	
	// load configuration from file
	load_config_file(&config);
	
	// define long options
	static struct option long_options[] = {
		{"help", no_argument, 0, 0},
		{"window_width", required_argument, 0, 'w'},
		{"window_height", required_argument, 0, 'h'},
		{"log_level", required_argument, 0, 'l'},
		{"audio_device", required_argument, 0, 'a'},
		{"sample_rate", required_argument, 0, 'r'},
		{0, 0, 0, 0}
	};
	
	// parse command line arguments
	int opt;
	int long_index = 0;
	while ((opt = getopt_long(argc, (char * const *)argv, "w:h:l:a:r:", long_options, &long_index)) != -1) {
		switch (opt) {
			case 0:
				if (strcmp("help", long_options[long_index].name) == 0) {
					print_help(argv[0]);
					exit(EXIT_SUCCESS);
				}
				break;
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
			case 'r':
				config.sample_rate = atoi(optarg);
				break;
			default:
				print_help(argv[0]);
				exit(EXIT_FAILURE);
		}
	}
	
	log_config_options(&config);
	
	ret = s9_app_create(&config);
	while (ret == 0) {
		ret = s9_app_run();
	}
	
	s9_app_destroy();
	
	if(config.audio_device) free(config.audio_device);
	
	return ret >= 0 ? 0 : ret;
}
