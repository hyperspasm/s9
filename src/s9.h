#ifndef s9_h
#define s9_h

#include <stdint.h>
#include <string.h>

typedef enum {
	LOG_LEVEL_TRACE    = 0,
	LOG_LEVEL_DEBUG    = 1,
	LOG_LEVEL_INFO     = 2,
	LOG_LEVEL_WARNING  = 3,
	LOG_LEVEL_ERROR    = 4,
	LOG_LEVEL_CRITICAL = 5,
} s9_log_level_t;

int s9_log(s9_log_level_t log_level, const char *format, ...);
int s9_print(const char *format, ...);

#define S9_STRINGIFY(x) #x
#define S9_TOSTRING(x) S9_STRINGIFY(x)
#define S9_BASE_FILENAME(file) (strrchr(file, '/') ? strrchr(file, '/') + 1 : (strrchr(file, '\\') ? strrchr(file, '\\') + 1 : file))

// error handling convenience macro
#define CHECK_ERROR(assertion, fail_action, message, ...) \
do { \
	if(!(assertion)) \
	{ \
		if(message) s9_log(LOG_LEVEL_ERROR, message " (%s line %d)", ##__VA_ARGS__, S9_BASE_FILENAME(__FILE__), __LINE__); \
		fail_action; \
	} \
} while(0)

#define CHECK_PARAMETER(assertion, fail_action) \
	do { \
		if (!(assertion)) { \
			s9_log(LOG_LEVEL_ERROR, "bad parameter: " S9_TOSTRING(assertion) " (%s line %d)", S9_BASE_FILENAME(__FILE__), __LINE__); \
			fail_action; \
		} \
	} while (0)

#endif /* s9_h */
