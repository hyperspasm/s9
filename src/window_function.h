#ifndef window_function_h
#define window_function_h

#include <stdint.h>

// enum for different window functions
typedef enum {
	WINDOW_FUNCTION_BARTLETT,
	WINDOW_FUNCTION_HANN,
	WINDOW_FUNCTION_WELCH,
	WINDOW_FUNCTION_HAMMING,
	WINDOW_FUNCTION_BLACKMAN_HARRIS,
	WINDOW_FUNCTION_BLACKMAN,
	WINDOW_FUNCTION_RECTANGULAR
} window_function_t;

// create a window function
void create_window_function(window_function_t function, uint32_t length, int normalize, float *out_window);

#endif /* window_function_h */
