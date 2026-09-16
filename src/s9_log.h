#ifndef s9_log_h
#define s9_log_h

// different logging levels
typedef enum {
	LOG_LEVEL_TRACE    = 0,
	LOG_LEVEL_DEBUG    = 1,
	LOG_LEVEL_INFO     = 2,
	LOG_LEVEL_WARNING  = 3,
	LOG_LEVEL_ERROR    = 4,
	LOG_LEVEL_CRITICAL = 5,
} s9_log_level_t;

// write to the logs, CR/LF is automatically appended
int s9_log(s9_log_level_t log_level, const char *format, ...);

// write to the console
int s9_print(const char *format, ...);

// convenience macros
#define S9_LOG_TRACE(format, ...) 		s9_log(LOG_LEVEL_TRACE, format, ##__VA_ARGS__)
#define S9_LOG_DEBUG(format, ...) 		s9_log(LOG_LEVEL_DEBUG, format, ##__VA_ARGS__)
#define S9_LOG_INFO(format, ...) 		s9_log(LOG_LEVEL_INFO, format, ##__VA_ARGS__)
#define S9_LOG_WARNING(format, ...) 	s9_log(LOG_LEVEL_WARNING, format, ##__VA_ARGS__)
#define S9_LOG_ERROR(format, ...) 		s9_log(LOG_LEVEL_ERROR, format, ##__VA_ARGS__)
#define S9_LOG_CRITICAL(format, ...)	s9_log(LOG_LEVEL_CRITICAL, format, ##__VA_ARGS__)


// -- logging and console customization

// set the current logging level (LOG_LEVEL_INFO is default)
void s9_set_log_level(s9_log_level_t log_level);

// by default s9_log() prints to stderr
// this function allows overriding the behavior 
void s9_set_logging_function(void (*log_line_func)(const char *text));

// by default s9_print() prints to stdout
// this function allows overriding the behavior
void s9_set_console_print_function(void (*console_print_func)(const char *text));

#endif /* s9_log_h */
