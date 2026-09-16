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
	WINDOW_FUNCTION_RECTANGULAR,
	WINDOW_FUNCTION_COUNT
} window_function_t;

static const char *const window_function_names[WINDOW_FUNCTION_COUNT] = {
	[WINDOW_FUNCTION_BARTLETT]        = "bartlett",
	[WINDOW_FUNCTION_HANN]            = "hann",
	[WINDOW_FUNCTION_WELCH]           = "welch",
	[WINDOW_FUNCTION_HAMMING]         = "hamming",
	[WINDOW_FUNCTION_BLACKMAN_HARRIS] = "blackman_harris",
	[WINDOW_FUNCTION_BLACKMAN]        = "blackman",
	[WINDOW_FUNCTION_RECTANGULAR]     = "rectangular",
};

// create a window function
// skew: 0.0 = symmetric (no warping), positive values shift peak toward
//       the end of the window for temporal recency bias.
//       Practical range is 0.0 to ~3.0.
void create_window_function(window_function_t function, uint32_t length,
							int normalize, float skew, float *out_window);

// The width of a window's transform at half its peak, in bins of the
// window's own length: the width a tone's ridge has through it. A property
// of the shape, the same at any length.
double window_bandwidth(const float *window, uint32_t length);

// the same for a window function's symmetric shape, measured once per function and kept
double window_function_bandwidth(window_function_t function);

#endif /* window_function_h */
