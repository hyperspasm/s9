#include "s9.h"
#include <stdio.h>
#include <stdarg.h>

// weakly define s9_log function
__attribute__((weak)) int s9_log(s9_log_level_t log_level, const char *format, ...) {
	va_list args;
	va_start(args, format);
	int ret = vprintf(format, args);
	va_end(args);
	return ret;
}

// weakly define s9_print function
__attribute__((weak)) int s9_print(const char *format, ...) {
	va_list args;
	va_start(args, format);
	int ret = vprintf(format, args);
	va_end(args);
	return ret;
}
