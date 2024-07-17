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

extern int s9_log(s9_log_level_t log_level, const char *format, ...);
extern int s9_print(const char *format, ...);

#define S9_LOG_TRACE(format, ...) 		s9_log(LOG_LEVEL_TRACE, format, ##__VA_ARGS__)
#define S9_LOG_DEBUG(format, ...) 		s9_log(LOG_LEVEL_DEBUG, format, ##__VA_ARGS__)
#define S9_LOG_INFO(format, ...) 		s9_log(LOG_LEVEL_INFO, format, ##__VA_ARGS__)
#define S9_LOG_WARNING(format, ...) 	s9_log(LOG_LEVEL_WARNING, format, ##__VA_ARGS__)
#define S9_LOG_ERROR(format, ...) 		s9_log(LOG_LEVEL_ERROR, format, ##__VA_ARGS__)
#define S9_LOG_CRITICAL(format, ...)	s9_log(LOG_LEVEL_CRITICAL, format, ##__VA_ARGS__)

#define S9_STRINGIFY(x) #x
#define S9_TOSTRING(x) S9_STRINGIFY(x)
#define S9_BASE_FILENAME(file) (strrchr(file, '/') ? strrchr(file, '/') + 1 : (strrchr(file, '\\') ? strrchr(file, '\\') + 1 : file))

// error handling convenience macro
#define CHECK_ERROR(assertion, fail_action, message, ...) \
do { \
	if(!(assertion)) \
	{ \
		if(message) S9_LOG_ERROR(message " (%s: %d)", ##__VA_ARGS__, S9_BASE_FILENAME(__FILE__), __LINE__); \
		fail_action; \
	} \
} while(0)

#define CHECK_PARAMETER(assertion, fail_action) \
	do { \
		if (!(assertion)) { \
			S9_LOG_ERROR("bad parameter: " S9_TOSTRING(assertion) " (%s: %d)", S9_BASE_FILENAME(__FILE__), __LINE__); \
			fail_action; \
		} \
	} while (0)

#define CHECK_ALLOC(ptr, fail_action) \
	do { \
		if (ptr == NULL) { \
			S9_LOG_ERROR("allocation failed (%s: %d)", S9_BASE_FILENAME(__FILE__), __LINE__); \
			fail_action; \
		} \
	} while (0)

#endif /* s9_h */
