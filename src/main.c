#include <stdio.h>

#include "s9_app.h"


static void log_line(const char *text) {
	fprintf(stderr, "%s", text);
}

static void console_print(const char *text) {
	fprintf(stdout, "%s", text);
}

int main(int argc, const char * argv[]) {
	int ret;
	
	s9_app_config_t config = {
		.log_line = log_line,
		.log_level = LOG_LEVEL_TRACE,
		.console_print = console_print,
		
		.window_width = 1920,
		.window_height = 1080,
	};
	
	ret = s9_app_create(&config);
	while(ret == 0) {
		ret = s9_app_run();
	}
	
	s9_app_destroy();
	
	return ret >= 0 ? 0 : ret;
}
