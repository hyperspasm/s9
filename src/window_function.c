#include "window_function.h"
#include <math.h>
#include <stddef.h>

#define _PI   ((double)3.14159265358979323846)
#define _2PI  ((double)6.28318530717958647692)
#define _4PI  ((double)4.0 * _PI)
#define _6PI  ((double)6.0 * _PI)

// https://www.gaussianwaves.com/2020/09/window-function-figure-of-merits/

static void bartlett(uint32_t length, float* out_window) {
	uint32_t i;
	
	if (length < 1 || out_window == NULL) return;
	if (length == 1) {
		out_window[0] = 1.0f;
		return;
	}
	
	length--;
	double half_length = 0.5 * length;
	
	for (i = 0; i <= length; i++) {
		out_window[i] = (float)(1.0 - fabs((i - half_length) / half_length));
	}
}

static void hann(uint32_t length, float* out_window) {
	uint32_t i;
	
	if (length < 1 || out_window == NULL) return;
	if (length == 1) {
		out_window[0] = 1.0f;
		return;
	}
	
	length--;
	double factor2 = _2PI / length;
	
	for (i = 0; i <= length; i++) {
		out_window[i] = (float)(0.5 - 0.5 * cos(factor2 * i));
	}
}

static void welch(uint32_t length, float* out_window) {
	uint32_t i;
	
	if (length < 1 || out_window == NULL) return;
	if (length == 1) {
		out_window[0] = 1.0f;
		return;
	}
	
	length--;
	double half_length = 0.5 * length;
	
	for (i = 0; i <= length; i++) {
		out_window[i] = (float)(1.0 - pow((i - half_length) / half_length, 2.0));
	}
}

static void hamming(uint32_t length, float* out_window) {
	uint32_t i;
	
	if (length < 1 || out_window == NULL) return;
	if (length == 1) {
		out_window[0] = 1.0f;
		return;
	}
	
	length--;
	double factor2 = _2PI / length;
	
	for (i = 0; i <= length; i++) {
		out_window[i] = (float)(0.54 - 0.46 * cos(factor2 * i));
	}
}

static void blackman_harris(uint32_t length, float* out_window) {
	uint32_t i;
	
	if (length < 1 || out_window == NULL) return;
	if (length == 1) {
		out_window[0] = 1.0f;
		return;
	}
	
	length--;
	double factor2 = _2PI / length;
	double factor4 = _4PI / length;
	double factor6 = _6PI / length;
	
	for (i = 0; i <= length; i++) {
		out_window[i] = (float)(0.35875 - 0.48829 * cos(factor2 * i) +
								0.14128 * cos(factor4 * i) -
								0.01168 * cos(factor6 * i));
	}
}

static void blackman(uint32_t length, float* out_window) {
	uint32_t i;
	
	if (length < 1 || out_window == NULL) return;
	if (length == 1) {
		out_window[0] = 1.0f;
		return;
	}
	
	length--;
	double factor2 = _2PI / length;
	double factor4 = _4PI / length;
	
	for (i = 0; i <= length; i++) {
		out_window[i] = (float)(0.42323 - 0.49755 * cos(factor2 * i) +
								0.07922 * cos(factor4 * i));
	}
}

static void rectangular(uint32_t length, float* out_window) {
	uint32_t i;
	
	if (length < 1 || out_window == NULL) return;
	for (i = 0; i < length; i++) {
		out_window[i] = 1.0f;
	}
}

static void normalize_window(float *window, uint32_t length) {
	// compute the sum of the window function values
	float sum = 0.0f;
	for (uint32_t i = 0; i < length; i++) {
		sum += window[i];
	}
	
	// compute the normalization factor
	float normalization_factor = (float)length / sum;
	
	// apply the normalization factor to the window function
	for (uint32_t i = 0; i < length; i++) {
		window[i] *= normalization_factor;
	}
}

void create_window_function(window_function_t function, uint32_t length, int normalize, float *out_window) {
	switch (function) {
		case WINDOW_FUNCTION_BARTLETT:
			bartlett(length, out_window);
			break;
		case WINDOW_FUNCTION_HANN:
			hann(length, out_window);
			break;
		case WINDOW_FUNCTION_WELCH:
			welch(length, out_window);
			break;
		case WINDOW_FUNCTION_HAMMING:
			hamming(length, out_window);
			break;
		case WINDOW_FUNCTION_BLACKMAN_HARRIS:
			blackman_harris(length, out_window);
			break;
		case WINDOW_FUNCTION_BLACKMAN:
			blackman(length, out_window);
			break;
		case WINDOW_FUNCTION_RECTANGULAR:
		default:
			rectangular(length, out_window);
			break;
	}
	
	if(normalize) {
		normalize_window(out_window, length);
	}
}
