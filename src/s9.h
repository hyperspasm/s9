#ifndef s9_h
#define s9_h

#include <stdint.h>
#include <string.h>

#include "s9_log.h"


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
