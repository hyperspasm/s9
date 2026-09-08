#include "fft_analyzer.h"
#include "s9.h"
#include "ring_buffer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define QUEUE_DISPLAY_FRAMES 10   // audio held between updates, in display frames

struct fft_analyzer {
	fft_analyzer_config_t config;
	uint32_t channel;             // FFT_ANALYZER_MIX: the channel transformed

	int streaming;
	s9_audio_stream_config_t stream;
	ring_buffer_t *rb;            // interleaved frames from the audio thread
	float *input;                 // frames dequeued by one update
	uint32_t fft_len;
	float *history[2];            // the last fft_len frames, interleaved; two so one can be shifted into the other
	int current;                  // the history holding the latest frames
	float *window;                // fft_len, normalized
	float *frame;                 // fft_len: the windowed mono frame handed to the transform
	uint32_t transforms;
	fftwf_complex **spectra;      // transforms x (fft_len/2 + 1)
	fftwf_plan plan;              // frame -> spectrum

	fftwf_plan inverse;           // power -> acf
	fftwf_complex *power;         // fft_len/2 + 1
	float *acf;                   // fft_len
	float *window_acf;            // fft_len: the window's own autocorrelation, same scaling as acf
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

// -- lifecycle

fft_analyzer_t *fft_analyzer_new(const fft_analyzer_config_t *config) {
	fft_analyzer_t *fa = malloc(sizeof(fft_analyzer_t));
	CHECK_ALLOC(fa, return NULL);
	memset(fa, 0, sizeof(*fa));
	fa->config = *config;
	fa->channel = config->channel;
	return fa;
}

static void free_dsp_allocations(fft_analyzer_t *fa) {
	fa->streaming = 0;
	fa->stream.audio_format.sample_type = S9_SAMPLE_NONE;

	if (fa->rb) ring_buffer_free(&fa->rb);
	free(fa->input); fa->input = NULL;
	free(fa->history[0]); fa->history[0] = NULL;
	free(fa->history[1]); fa->history[1] = NULL;
	free(fa->window); fa->window = NULL;
	free(fa->window_acf); fa->window_acf = NULL;
	if (fa->frame) { fftwf_free(fa->frame); fa->frame = NULL; }
	if (fa->spectra) {
		for (uint32_t i = 0; i < fa->transforms; i++)
			if (fa->spectra[i]) fftwf_free(fa->spectra[i]);
		free(fa->spectra);
		fa->spectra = NULL;
	}
	fa->transforms = 0;
	if (fa->power) { fftwf_free(fa->power); fa->power = NULL; }
	if (fa->acf) { fftwf_free(fa->acf); fa->acf = NULL; }
	if (fa->plan) { fftwf_destroy_plan(fa->plan); fa->plan = NULL; }
	if (fa->inverse) { fftwf_destroy_plan(fa->inverse); fa->inverse = NULL; }
}

void fft_analyzer_free(fft_analyzer_t **fa) {
	if (fa && *fa) {
		free_dsp_allocations(*fa);
		free(*fa);
		*fa = NULL;
	}
}

void fft_analyzer_set_channel(fft_analyzer_t *fa, uint32_t channel) {
	fa->channel = channel;
}

uint32_t fft_analyzer_channel(const fft_analyzer_t *fa) {
	return fa->channel;
}

// -- streaming

// the window's autocorrelation, through the same transforms the frames use
static void compute_window_acf(fft_analyzer_t *fa) {
	uint32_t bins = fa->fft_len / 2 + 1;
	memcpy(fa->frame, fa->window, sizeof(float) * fa->fft_len);
	fftwf_execute_dft_r2c(fa->plan, fa->frame, fa->power);
	for (uint32_t k = 0; k < bins; k++) {
		fa->power[k][0] = fa->power[k][0] * fa->power[k][0] + fa->power[k][1] * fa->power[k][1];
		fa->power[k][1] = 0.0f;
	}
	fftwf_execute(fa->inverse);
	memcpy(fa->window_acf, fa->acf, sizeof(float) * fa->fft_len);
}

void fft_analyzer_prepare_to_stream(fft_analyzer_t *fa, const s9_audio_stream_config_t *stream_config) {
	CHECK_PARAMETER(stream_config->audio_format.sample_type == S9_SAMPLE_FLOAT32, return);
	if (fa->streaming && S9_AUDIO_STREAM_CONFIG_EQUALS(fa->stream, *stream_config)) return;

	free_dsp_allocations(fa);

	uint32_t channels = stream_config->channels;
	fa->fft_len = fa->config.fft_len ? fa->config.fft_len
	                                 : next_power_of_2((uint32_t)((float)stream_config->sample_rate / fa->config.resolution_hz));
	S9_LOG_DEBUG("fft_analyzer: %u point transform", fa->fft_len);

	uint32_t bytes_per_frame = stream_config->audio_format.bytes_per_sample * channels;
	uint32_t queue_bytes = QUEUE_DISPLAY_FRAMES * bytes_per_frame * stream_config->samples_per_display_frame;
	fa->rb = ring_buffer_new(queue_bytes);
	CHECK_ALLOC(fa->rb, return);
	fa->input = malloc(queue_bytes);
	CHECK_ALLOC(fa->input, return);

	for (int i = 0; i < 2; i++) {
		fa->history[i] = calloc(fa->fft_len * channels, sizeof(float));
		CHECK_ALLOC(fa->history[i], return);
	}
	fa->current = 0;

	fa->window = malloc(sizeof(float) * fa->fft_len);
	CHECK_ALLOC(fa->window, return);
	create_window_function(fa->config.window_function, fa->fft_len, 1, fa->config.window_skew, fa->window);

	fa->frame = fftwf_malloc(sizeof(float) * fa->fft_len);
	CHECK_ALLOC(fa->frame, return);

	fa->transforms = fa->config.mode == FFT_ANALYZER_MIX ? 1 : channels;
	fa->spectra = calloc(fa->transforms, sizeof(*fa->spectra));
	CHECK_ALLOC(fa->spectra, return);
	uint32_t bins = fa->fft_len / 2 + 1;
	for (uint32_t i = 0; i < fa->transforms; i++) {
		fa->spectra[i] = fftwf_malloc(sizeof(fftwf_complex) * bins);
		CHECK_ALLOC(fa->spectra[i], return);
		memset(fa->spectra[i], 0, sizeof(fftwf_complex) * bins);
	}
	fa->plan = fftwf_plan_dft_r2c_1d((int)fa->fft_len, fa->frame, fa->spectra[0], FFTW_ESTIMATE);
	CHECK_ERROR(fa->plan != NULL, return, "fftwf_plan_dft_r2c_1d() failed");

	fa->power = fftwf_malloc(sizeof(fftwf_complex) * bins);
	CHECK_ALLOC(fa->power, return);
	fa->acf = fftwf_malloc(sizeof(float) * fa->fft_len);
	CHECK_ALLOC(fa->acf, return);
	fa->window_acf = malloc(sizeof(float) * fa->fft_len);
	CHECK_ALLOC(fa->window_acf, return);
	fa->inverse = fftwf_plan_dft_c2r_1d((int)fa->fft_len, fa->power, fa->acf, FFTW_ESTIMATE);
	CHECK_ERROR(fa->inverse != NULL, return, "fftwf_plan_dft_c2r_1d() failed");
	compute_window_acf(fa);

	fa->stream = *stream_config;
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
	uint32_t channel = fa->config.mode == FFT_ANALYZER_MIX ? fa->channel : index;

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

void fft_analyzer_update(fft_analyzer_t *fa) {
	if (!fa->streaming) return;

	uint32_t channels = fa->stream.channels;
	uint32_t bytes_per_frame = fa->stream.audio_format.bytes_per_sample * channels;
	uint32_t frames_avail = ring_buffer_read_avail(fa->rb) / bytes_per_frame;

	// consume whole display frames
	uint32_t consume = frames_avail;
	if (frames_avail > fa->stream.samples_per_display_frame)
		consume = (frames_avail / fa->stream.samples_per_display_frame) * fa->stream.samples_per_display_frame;
	uint32_t dequeued = ring_buffer_read(fa->rb, (uint8_t *)fa->input, consume * bytes_per_frame);
	S9_ASSERT(dequeued == consume * bytes_per_frame);

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

	for (uint32_t i = 0; i < fa->transforms; i++) {
		mono_frame(fa, i, fa->frame);
		for (uint32_t j = 0; j < fa->fft_len; j++) fa->frame[j] *= fa->window[j];
		fftwf_execute_dft_r2c(fa->plan, fa->frame, fa->spectra[i]);
	}
}

uint32_t fft_analyzer_fft_len(const fft_analyzer_t *fa) { return fa->fft_len; }
uint32_t fft_analyzer_sample_rate(const fft_analyzer_t *fa) { return fa->stream.sample_rate; }
uint32_t fft_analyzer_transforms(const fft_analyzer_t *fa) { return fa->transforms; }

const fftwf_complex *fft_analyzer_spectrum(const fft_analyzer_t *fa, uint32_t index) {
	return index < fa->transforms ? fa->spectra[index] : NULL;
}

const float *fft_analyzer_window(const fft_analyzer_t *fa) { return fa->window; }

float fft_analyzer_rms(const fft_analyzer_t *fa, uint32_t index) {
	if (!fa->streaming || index >= fa->transforms) return 0.0f;
	mono_frame(fa, index, fa->frame);
	double sum = 0.0;
	for (uint32_t i = 0; i < fa->fft_len; i++) sum += (double)fa->frame[i] * fa->frame[i];
	return (float)sqrt(sum / fa->fft_len);
}

int fft_analyzer_nacf(fft_analyzer_t *fa, uint32_t index, float *nacf, uint32_t lags) {
	if (!fa->streaming || index >= fa->transforms || lags > fa->fft_len) return -1;

	uint32_t bins = fa->fft_len / 2 + 1;
	const fftwf_complex *spectrum = fa->spectra[index];
	for (uint32_t k = 0; k < bins; k++) {
		fa->power[k][0] = spectrum[k][0] * spectrum[k][0] + spectrum[k][1] * spectrum[k][1];
		fa->power[k][1] = 0.0f;
	}
	fftwf_execute(fa->inverse);

	float scale = fa->window_acf[0] > 0.0f && fa->acf[0] > 0.0f ? fa->window_acf[0] / fa->acf[0] : 0.0f;
	for (uint32_t t = 0; t < lags; t++)
		nacf[t] = fa->window_acf[t] > 0.0f ? fa->acf[t] / fa->window_acf[t] * scale : 0.0f;
	return 0;
}
