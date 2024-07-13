//
//  s9_common.h
//  s9
//
//  Created by shea on 7/7/24.
//

#ifndef s9_h
#define s9_h

#include <stdint.h>

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

// error handling convenience macro
#define CHECK_ERROR(assertion, fail_action, message, ...) \
do { \
	if(!(assertion)) \
	{ \
		if(message) s9_log(LOG_LEVEL_ERROR, message, ##__VA_ARGS__); \
		fail_action; \
	} \
} while(0)

#endif /* s9_h */
