#include "fft_analyzer.h"
#include "s9.h"
#include "ring_buffer.h"

#include <fftw3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define QUEUE_DISPLAY_FRAMES 10   // audio held between updates, in display frames

struct fft_analyzer {
	fft_analyzer_config_t config;    // as last set
	fft_analyzer_config_t built;     // what the transform was set up for

	int streaming;
	s9_audio_stream_config_t stream;

	// the queue, shared with the audio thread
	ring_buffer_t *rb;            // interleaved frames from the audio thread
	float *input;                 // frames dequeued by one update

	// the transform, the render thread's alone
	uint32_t fft_len;
	float *history[2];            // the last fft_len frames, interleaved; two so one can be shifted into the other
	int current;                  // the history holding the latest frames
	float *window;                // fft_len, normalized
	float *frame;                 // fft_len: the windowed mono frame handed to the transform
	uint32_t transforms;
	fftwf_complex **spectra;      // transforms x (fft_len/2 + 1)
	fftwf_plan plan;              // frame -> spectrum
	float **power;                // transforms x (fft_len/2 + 1): the averaged power
	int power_primed;             // the average holds a transform
	float fraction;               // how far the last update moved the average toward its transform
	uint32_t consumed;            // frames consumed since the transform was set up, until the history filled
	float hop_s;                  // audio the last update consumed, seconds
};

static uint32_t next_power_of_2(uint32_t n) {
	if (n == 0) return 1;
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	return n + 1;
}

// -- allocation

static void free_queue(fft_analyzer_t *fa) {
	if (fa->rb) ring_buffer_free(&fa->rb);
	free(fa->input); fa->input = NULL;
}

static int alloc_queue(fft_analyzer_t *fa) {
	uint32_t bytes_per_frame = fa->stream.audio_format.bytes_per_sample * fa->stream.channels;
	uint32_t queue_bytes = QUEUE_DISPLAY_FRAMES * bytes_per_frame * fa->stream.samples_per_display_frame;
	fa->rb = ring_buffer_new(queue_bytes);
	CHECK_ALLOC(fa->rb, return -1);
	fa->input = malloc(queue_bytes);
	CHECK_ALLOC(fa->input, return -1);
	return 0;
}

static void free_transform(fft_analyzer_t *fa) {
	free(fa->history[0]); fa->history[0] = NULL;
	free(fa->history[1]); fa->history[1] = NULL;
	free(fa->window); fa->window = NULL;
	if (fa->frame) { fftwf_free(fa->frame); fa->frame = NULL; }
	if (fa->spectra) {
		for (uint32_t i = 0; i < fa->transforms; i++)
			if (fa->spectra[i]) fftwf_free(fa->spectra[i]);
		free(fa->spectra);
		fa->spectra = NULL;
	}
	if (fa->power) {
		for (uint32_t i = 0; i < fa->transforms; i++) free(fa->power[i]);
		free(fa->power);
		fa->power = NULL;
	}
	fa->power_primed = 0;
	fa->transforms = 0;
	if (fa->plan) { fftwf_destroy_plan(fa->plan); fa->plan = NULL; }
}

// whether the transform must be set up again for a configuration
static int needs_rebuild(const fft_analyzer_config_t *built, const fft_analyzer_config_t *config) {
	return config->fft_len != built->fft_len || config->resolution_hz != built->resolution_hz ||
	       config->window_function != built->window_function || config->window_skew != built->window_skew ||
	       config->mode != built->mode;
}

static int alloc_transform(fft_analyzer_t *fa) {
	fa->built = fa->config;
	uint32_t channels = fa->stream.channels;
	fa->fft_len = fa->config.fft_len ? fa->config.fft_len
	                                 : next_power_of_2((uint32_t)((float)fa->stream.sample_rate / fa->config.resolution_hz));
	S9_LOG_DEBUG("fft_analyzer: %u point transform", fa->fft_len);

	for (int i = 0; i < 2; i++) {
		fa->history[i] = calloc(fa->fft_len * channels, sizeof(float));
		CHECK_ALLOC(fa->history[i], return -1);
	}
	fa->current = 0;
	fa->consumed = 0;

	fa->window = malloc(sizeof(float) * fa->fft_len);
	CHECK_ALLOC(fa->window, return -1);
	create_window_function(fa->config.window_function, fa->fft_len, 1, fa->config.window_skew, fa->window);

	fa->frame = fftwf_malloc(sizeof(float) * fa->fft_len);
	CHECK_ALLOC(fa->frame, return -1);

	uint32_t transforms = fa->config.mode == FFT_ANALYZER_MIX ? 1 : channels;
	fa->spectra = calloc(transforms, sizeof(*fa->spectra));
	CHECK_ALLOC(fa->spectra, return -1);
	fa->transforms = transforms;
	uint32_t bins = fa->fft_len / 2 + 1;
	for (uint32_t i = 0; i < transforms; i++) {
		fa->spectra[i] = fftwf_malloc(sizeof(fftwf_complex) * bins);
		CHECK_ALLOC(fa->spectra[i], return -1);
		memset(fa->spectra[i], 0, sizeof(fftwf_complex) * bins);
	}
	fa->plan = fftwf_plan_dft_r2c_1d((int)fa->fft_len, fa->frame, fa->spectra[0], FFTW_ESTIMATE);
	CHECK_ERROR(fa->plan != NULL, return -1, "fftwf_plan_dft_r2c_1d() failed");

	fa->power = calloc(transforms, sizeof(*fa->power));
	CHECK_ALLOC(fa->power, return -1);
	for (uint32_t i = 0; i < transforms; i++) {
		fa->power[i] = calloc(bins, sizeof(float));
		CHECK_ALLOC(fa->power[i], return -1);
	}
	fa->power_primed = 0;
	return 0;
}

// -- lifecycle

fft_analyzer_t *fft_analyzer_new(const fft_analyzer_config_t *config) {
	fft_analyzer_t *fa = malloc(sizeof(fft_analyzer_t));
	CHECK_ALLOC(fa, return NULL);
	memset(fa, 0, sizeof(*fa));
	fa->config = *config;
	return fa;
}

void fft_analyzer_free(fft_analyzer_t **fa) {
	if (fa && *fa) {
		free_transform(*fa);
		free_queue(*fa);
		free(*fa);
		*fa = NULL;
	}
}

void fft_analyzer_set_config(fft_analyzer_t *fa, const fft_analyzer_config_t *config) {
	fa->config = *config;
}

const fft_analyzer_config_t *fft_analyzer_config(const fft_analyzer_t *fa) {
	return &fa->config;
}

// -- streaming

void fft_analyzer_prepare_to_stream(fft_analyzer_t *fa, const s9_audio_stream_config_t *stream_config) {
	CHECK_PARAMETER(stream_config->audio_format.sample_type == S9_SAMPLE_FLOAT32, return);
	if (fa->streaming && S9_AUDIO_STREAM_CONFIG_EQUALS(fa->stream, *stream_config)) return;

	fa->streaming = 0;
	free_transform(fa);
	free_queue(fa);
	fa->stream = *stream_config;
	if (alloc_queue(fa) != 0 || alloc_transform(fa) != 0) {
		free_transform(fa);
		free_queue(fa);
		return;
	}
	fa->streaming = 1;
}

void fft_analyzer_stop_streaming(fft_analyzer_t *fa) {
	fa->streaming = 0;
}

int fft_analyzer_streaming(const fft_analyzer_t *fa) {
	return fa->streaming;
}

void fft_analyzer_enqueue_audio_data(fft_analyzer_t *fa, const void *data, uint32_t size) {
	if (fa->rb) ring_buffer_write(fa->rb, data, size);
}

// -- transforms

// the frame behind a transform index, before windowing, into dst
static void mono_frame(const fft_analyzer_t *fa, uint32_t index, float *dst) {
	uint32_t channels = fa->stream.channels;
	const float *src = fa->history[fa->current];
	uint32_t channel = fa->config.mode == FFT_ANALYZER_MIX ? fa->config.channel : index;

	if (channel < channels) {
		for (uint32_t i = 0; i < fa->fft_len; i++) dst[i] = src[i * channels + channel];
	} else {
		// the channels averaged
		float scale = 1.0f / (float)channels;
		for (uint32_t i = 0; i < fa->fft_len; i++) {
			float sum = 0.0f;
			for (uint32_t c = 0; c < channels; c++) sum += src[i * channels + c];
			dst[i] = sum * scale;
		}
	}
}

uint32_t fft_analyzer_update(fft_analyzer_t *fa) {
	if (!fa->streaming) return 0;

	if (needs_rebuild(&fa->built, &fa->config)) {
		free_transform(fa);
		if (alloc_transform(fa) != 0) {
			free_transform(fa);
			fa->streaming = 0;
			return 0;
		}
	}

	uint32_t channels = fa->stream.channels;
	uint32_t bytes_per_frame = fa->stream.audio_format.bytes_per_sample * channels;
	uint32_t frames_avail = ring_buffer_read_avail(fa->rb) / bytes_per_frame;

	// consume one hop, or whole display frames
	uint32_t consume = frames_avail;
	if (fa->config.hop_frames) {
		if (frames_avail < fa->config.hop_frames) {
			fa->hop_s = 0.0f;
			fa->fraction = 0.0f;
			return 0;
		}
		consume = fa->config.hop_frames;
	} else if (frames_avail > fa->stream.samples_per_display_frame) {
		consume = (frames_avail / fa->stream.samples_per_display_frame) * fa->stream.samples_per_display_frame;
	}
	uint32_t dequeued = ring_buffer_read(fa->rb, (uint8_t *)fa->input, consume * bytes_per_frame);
	S9_ASSERT(dequeued == consume * bytes_per_frame);
	fa->hop_s = (float)consume / (float)fa->stream.sample_rate;
	int was_full = fa->consumed >= fa->fft_len;   // the frame before this update was whole
	if (fa->consumed < fa->fft_len) fa->consumed += consume < fa->fft_len - fa->consumed ? consume : fa->fft_len - fa->consumed;

	// slide the history: keep what still fits, append the new frames; drop
	// what cannot be caught up with
	float *next = fa->history[fa->current == 0];
	const float *last = fa->history[fa->current];
	fa->current = fa->current == 0;
	uint32_t dropped = 0;
	if (consume > fa->fft_len) {
		dropped = consume - fa->fft_len;
		consume = fa->fft_len;
	}
	uint32_t kept = fa->fft_len - consume;
	if (kept) memcpy(next, last + consume * channels, kept * bytes_per_frame);
	if (consume) memcpy(next + kept * channels, fa->input + dropped * channels, consume * bytes_per_frame);

	// the fraction of the way the average moves toward this transform's
	// power: all the way until the history has filled and the average holds
	// a whole frame's transform
	fa->fraction = 1.0f;
	if (fa->power_primed && was_full && fa->config.averaging_s > 0.0f)
		fa->fraction = 1.0f - expf(-fa->hop_s / fa->config.averaging_s);
	float alpha = fa->fraction;

	uint32_t bins = fa->fft_len / 2 + 1;
	for (uint32_t i = 0; i < fa->transforms; i++) {
		mono_frame(fa, i, fa->frame);
		for (uint32_t j = 0; j < fa->fft_len; j++) fa->frame[j] *= fa->window[j];
		fftwf_execute_dft_r2c(fa->plan, fa->frame, fa->spectra[i]);

		const fftwf_complex *s = fa->spectra[i];
		float *avg = fa->power[i];
		for (uint32_t k = 0; k < bins; k++) {
			float p = s[k][0] * s[k][0] + s[k][1] * s[k][1];
			avg[k] += alpha * (p - avg[k]);
		}
	}
	fa->power_primed = 1;
	return consume + dropped;
}

uint32_t fft_analyzer_queued(fft_analyzer_t *fa) {
	if (!fa->rb) return 0;
	return ring_buffer_read_avail(fa->rb) / (fa->stream.audio_format.bytes_per_sample * fa->stream.channels);
}

int fft_analyzer_filled(const fft_analyzer_t *fa) {
	return fa->fft_len && fa->consumed >= fa->fft_len;
}

uint32_t fft_analyzer_fft_len(const fft_analyzer_t *fa) { return fa->fft_len; }
uint32_t fft_analyzer_sample_rate(const fft_analyzer_t *fa) { return fa->stream.sample_rate; }
uint32_t fft_analyzer_transforms(const fft_analyzer_t *fa) { return fa->transforms; }

const float *fft_analyzer_spectrum(const fft_analyzer_t *fa, uint32_t index) {
	return index < fa->transforms ? (const float *)fa->spectra[index] : NULL;
}

const float *fft_analyzer_power(const fft_analyzer_t *fa, uint32_t index) {
	return index < fa->transforms ? fa->power[index] : NULL;
}

void fft_analyzer_reset_power(fft_analyzer_t *fa) {
	fa->power_primed = 0;
}

float fft_analyzer_average_fraction(const fft_analyzer_t *fa) {
	return fa->fraction;
}

float fft_analyzer_hop_seconds(const fft_analyzer_t *fa) { return fa->hop_s; }

const float *fft_analyzer_window(const fft_analyzer_t *fa) { return fa->window; }

const float *fft_analyzer_frame(fft_analyzer_t *fa, uint32_t index) {
	if (!fa->streaming || index >= fa->transforms) return NULL;
	mono_frame(fa, index, fa->frame);
	return fa->frame;
}

float fft_analyzer_rms(const fft_analyzer_t *fa, uint32_t index) {
	if (!fa->streaming || index >= fa->transforms) return 0.0f;
	mono_frame(fa, index, fa->frame);
	double sum = 0.0;
	for (uint32_t i = 0; i < fa->fft_len; i++) sum += (double)fa->frame[i] * fa->frame[i];
	return (float)sqrt(sum / fa->fft_len);
}
