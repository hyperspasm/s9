#include "s9.h"
#include "spectrogram.h"
#include "ring_buffer.h"
#include "fresp_graph.h"

#include <fftw3.h>

typedef struct spectrogram {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	spectrogram_config_t config;	
	TTF_Font* (*load_font)(int point_size);
	
	int streaming;
	s9_audio_stream_config_t stream_config;
	ring_buffer_t *rb;
	float *input_buffer;
	
	uint32_t fft_len;
	float *fft_input_buffers[2];		// interleaved (fft_len * n_channels)
	int current_fft_input_buffer_ix;
	float *fft_window_function_buffer; 	// fft_len	
	
	uint32_t n_fft_channels;
	float *fft_windowed_input_buffer;			// fft_len, 1 channel windowed input to fft
	fftwf_complex **fft_outputs;
	fftwf_plan plan;
	
	uint32_t n_graphs;
	fresp_graph_t **graphs;
	float *points;
	
} spectrogram_t;

#define WINDOW_SKEW 1.0f

void spectrogram_set_config(spectrogram_t *sg, const spectrogram_config_t *config) {
	if (config == NULL) {
		sg->config = default_spectrogram_config;
	} else {
		sg->config = *config;
	}
}

spectrogram_t *spectrogram_new(const spectrogram_config_t *config, TTF_Font* (*load_font)(int point_size)) {
	spectrogram_t *sg = (spectrogram_t *) malloc(sizeof(spectrogram_t));
	CHECK_ALLOC(sg, return NULL);
	memset(sg, 0, sizeof(*sg));
	
	sg->load_font = load_font;
	spectrogram_set_config(sg, config);
	
	return sg;
}

static void free_dsp_allocations(spectrogram_t *sg) {
	
	// clear flag and stream config
	sg->streaming = 0;
	sg->stream_config.audio_format.sample_type = S9_SAMPLE_NONE;
	
	// free allocations
	if(sg->rb) {
		ring_buffer_free(&sg->rb);
	}
	
	if(sg->input_buffer) {
		free(sg->input_buffer);
		sg->input_buffer = NULL;
	}
	
	if(sg->fft_input_buffers[0]) {
		fftwf_free(sg->fft_input_buffers[0]);
		sg->fft_input_buffers[0] = NULL;
	}
	
	if(sg->fft_input_buffers[1]) {
		fftwf_free(sg->fft_input_buffers[1]);
		sg->fft_input_buffers[1] = NULL;
	}
	
	sg->current_fft_input_buffer_ix = 0;
	
	if(sg->fft_window_function_buffer) {
		free(sg->fft_window_function_buffer);
		sg->fft_window_function_buffer = NULL;
	}
	
	if(sg->fft_windowed_input_buffer) {
		fftwf_free(sg->fft_windowed_input_buffer);
		sg->fft_windowed_input_buffer = NULL;
	}
	
	if(sg->fft_outputs) {
		for(uint32_t i = 0; i < sg->n_fft_channels; i++) {
			if(sg->fft_outputs[i]) {
				fftwf_free(sg->fft_outputs[i]);
				sg->fft_outputs[i] = NULL;
			}
		}		
		free(sg->fft_outputs);
		sg->fft_outputs = NULL;
		sg->n_fft_channels = 0;
	}
	
	if(sg->plan) {
		fftwf_destroy_plan(sg->plan);
		sg->plan = NULL;
	}
}

static void free_graphs(spectrogram_t *sg) {
	if( sg->n_graphs ) {
		for(uint32_t i=0; i < sg->n_graphs; i++) {
			fresp_graph_free(&(sg->graphs[i]));
		}
		sg->n_graphs = 0;
		
		free(sg->graphs);
		sg->graphs = NULL;
	}
}

void spectrogram_free(spectrogram_t **sg) {
	if(sg && *sg) {		
		free_dsp_allocations(*sg);
		free_graphs(*sg);
		free(*sg);
		*sg = NULL;
	}
}

static S9_FORCE_INLINE void prepare_to_draw(spectrogram_t *sg, SDL_Renderer *renderer) {
	SDL_Rect viewport;
	SDL_RenderGetViewport(renderer, &viewport);
	
	if(sg->renderer != renderer || !SDL_RectEquals(&sg->viewport, &viewport)) {
		sg->renderer = renderer;
		sg->viewport = viewport;
		
		free_graphs(sg);
		
		if(sg->n_fft_channels) {
			sg->graphs = malloc(sizeof(fresp_graph_t*) * sg->n_fft_channels);
			CHECK_ALLOC(sg->graphs, return);
			memset(sg->graphs, 0, sizeof(fresp_graph_t*) * sg->n_fft_channels);
			
			for(uint32_t i=0; i < sg->n_fft_channels; i++) {
				sg->graphs[i] = fresp_graph_new(&sg->config.graph_config, sg->load_font);
			}
			
			sg->n_graphs = sg->n_fft_channels;
		}
	}
}

static void compute_ffts(spectrogram_t *sg) {
	
	uint32_t n_channels = sg->stream_config.channels;
	uint32_t bytes_per_frame = sg->stream_config.audio_format.bytes_per_sample * n_channels;
	uint32_t bytes_avail = ring_buffer_read_avail(sg->rb);
	S9_ASSERT(bytes_avail % sg->stream_config.audio_format.bytes_per_sample * n_channels == 0);
	uint32_t frames_avail = bytes_avail / bytes_per_frame;		
	
	// compute how many frames to consume
	uint32_t frames_to_consume = frames_avail;
	if(frames_avail > sg->stream_config.samples_per_display_frame) {
		frames_to_consume = (frames_avail / sg->stream_config.samples_per_display_frame) * sg->stream_config.samples_per_display_frame;
	}
	
	// copy interleaved data out of the ring buffer to the input buffer
	uint32_t bytes_to_dequeue = frames_to_consume * bytes_per_frame;		
	uint32_t bytes_dequeued = ring_buffer_read(sg->rb, (uint8_t*)sg->input_buffer, bytes_to_dequeue);
	S9_ASSERT(bytes_dequeued == bytes_to_dequeue);
	
	
	
	// update and prepare current_fft_input_buffer 
	float *current_fft_input_buffer = sg->fft_input_buffers[sg->current_fft_input_buffer_ix];
	const float *last_fft_input_buffer = sg->fft_input_buffers[sg->current_fft_input_buffer_ix == 0];	
	sg->current_fft_input_buffer_ix = sg->current_fft_input_buffer_ix == 0; // toggle current_fft_input_buffer_ix for next time
	
	uint32_t n_history_frames = 0; // number of frames to re-use
	uint32_t n_frames_to_drop = 0; // number of frames that need to get dropped because we can't keep up (hopefully none)
	if(frames_to_consume < sg->fft_len) {
		n_history_frames = sg->fft_len - frames_to_consume;
	} else {
		// dropping frames  (defensive, hopefully this doesn't really happen)
		n_frames_to_drop = frames_to_consume - sg->fft_len;
		frames_to_consume = sg->fft_len;		  
	}
	
	// copy history to current_fft_input_buffer
	if(n_history_frames) {
		memcpy(&current_fft_input_buffer[0], &last_fft_input_buffer[(sg->fft_len - n_history_frames) * n_channels], n_history_frames * bytes_per_frame);		
	}
	
	// copy incoming frames to current_fft_input_buffer
	if(frames_to_consume) {
		memcpy(&current_fft_input_buffer[n_history_frames * n_channels], &sg->input_buffer[n_frames_to_drop * n_channels], frames_to_consume * bytes_per_frame);
	}
	
	// compute ffts for each channel
	for(uint32_t j=0; j < sg->n_fft_channels; j++) {
		float *src = current_fft_input_buffer + j;
		for(uint32_t i=0; i<sg->fft_len; i++) {
			sg->fft_windowed_input_buffer[i] = *src * sg->fft_window_function_buffer[i];
			src += sg->n_fft_channels;
		}
		
		fftwf_execute_dft_r2c(sg->plan, sg->fft_windowed_input_buffer, sg->fft_outputs[j]);
	}	
}

// this function receives a computed complex fft and fills the provided data buffer with dB magnitude values, distributed logarithmically (base 10) between min_freq and max_freq  
static void compute_plot_data(fftwf_complex *fft, uint32_t fft_len, uint32_t sample_rate, uint32_t min_freq, uint32_t max_freq, float *data, uint32_t data_len) {
	
	// precompute values to optimize for loop
	float bin_size_inv = (float)fft_len / (float)sample_rate;
	float log_min = log10f((float)min_freq);
	float log_max = log10f((float)max_freq);
	float inv_fft_len_half = 2.f / (float)fft_len;
	float inv_fft_len_quarter = inv_fft_len_half * inv_fft_len_half; 
	float log_range_inv_data_len = (log_max - log_min) / data_len;
	uint32_t half_fft_len = fft_len / 2;
	
	for (uint32_t i = 0; i < data_len; i++) {
		// calculate the logarithmic frequency range for the current index
		float log_freq_start = log_min + (i * log_range_inv_data_len);
		float log_freq_end = log_min + ((i + 1) * log_range_inv_data_len);
		float freq_start = powf(10.0f, log_freq_start);
		float freq_end = powf(10.0f, log_freq_end);
		
		// find the range of FFT bins for the current frequency range
		uint32_t bin_start = (uint32_t)(freq_start * bin_size_inv);
		uint32_t bin_end = (uint32_t)(freq_end * bin_size_inv);
		
		// limit the bin indices to valid range
		if (bin_start >= half_fft_len) {
			bin_start = half_fft_len - 1;
		}
		if (bin_end >= half_fft_len) {
			bin_end = half_fft_len - 1;
		}
		
		// find the maximum magnitude within the relevant frequency range
		float max_magnitude = 0.0f;
		for (uint32_t bin = bin_start; bin <= bin_end; bin++) {
			float real = fft[bin][0];
			float imag = fft[bin][1];
			float magnitude = real * real + imag * imag; // calculate magnitude squared
			if (magnitude > max_magnitude) {
				max_magnitude = magnitude;
			}
		}
		
		// normalize by fft_len / 2 and convert magnitude squared to dB
		float max_magnitude_db = 10.0f * log10f(max_magnitude * inv_fft_len_quarter); // since 10*log10(magnitude^2) == 20*log10(magnitude) we can avoid sqrt()
		
		// store the dB value in the data buffer
		data[i] = max_magnitude_db;
	}
}

typedef struct {
	spectrogram_t *sg;
	uint32_t channel;
} fresp_graph_data_callback_context_t;

static int fresp_graph_data_callback(void *context, float *plot_data, uint32_t n_points) {
	fresp_graph_data_callback_context_t *callback_context = (fresp_graph_data_callback_context_t*)context;
	
	spectrogram_t *sg = callback_context->sg;
	uint32_t channel = callback_context->channel;
	
	compute_plot_data(sg->fft_outputs[channel], sg->fft_len, sg->stream_config.sample_rate, sg->config.graph_config.min_freq, sg->config.graph_config.max_freq, plot_data, n_points);
	
	return 0;
}

void spectrogram_render(spectrogram_t *sg, SDL_Renderer *renderer) {
	if(!sg->streaming) return;
	prepare_to_draw(sg, renderer);
	
	compute_ffts(sg);
	
	SDL_Rect save_viewport, graph_viewport;
	SDL_RenderGetViewport(renderer, &save_viewport);
	
	graph_viewport = save_viewport;
	graph_viewport.h = save_viewport.h / sg->n_graphs;
	
	fresp_graph_data_callback_context_t callback_context = { .sg = sg };
	
	for(uint32_t i = 0; i < sg->n_graphs; i++) {
		SDL_RenderSetViewport(renderer, &graph_viewport);
		callback_context.channel = i;
		fresp_graph_render(sg->graphs[i], renderer, fresp_graph_data_callback, &callback_context);
		
		graph_viewport.y += graph_viewport.h;
	}
	
	SDL_RenderSetViewport(renderer, &save_viewport);
}

static uint32_t next_power_of_2(uint32_t n) {
	if (n == 0) {
		return 1;
	}
	n--;
	n = (n >> 1) | n;
	n = (n >> 2) | n;
	n = (n >> 4) | n;
	n = (n >> 8) | n;
	n = (n >> 16) | n;
	return n + 1;
}

void spectrogram_prepare_to_stream(spectrogram_t *sg, const s9_audio_stream_config_t *stream_config) {
	// only S9_AUDIO_FLOAT32 is supported
	CHECK_PARAMETER(stream_config->audio_format.sample_type == S9_SAMPLE_FLOAT32, return);
	
	if( S9_AUDIO_STREAM_CONFIG_EQUALS(sg->stream_config, *stream_config) ) {
		// no change
		return;
	}
	
	// free dsp allocations
	free_dsp_allocations(sg);
	
	// compute parameters for streaming
	
	sg->fft_len = next_power_of_2((uint32_t)((float)stream_config->sample_rate / sg->config.resolution_hz));
	S9_LOG_DEBUG("spectrogram %d fft length", sg->fft_len);
	
	
	// allocate for streaming
	
	// compute number of bytes necessary to store 10 video frames of audio data
	uint32_t size_bytes = 10;
	size_bytes *= stream_config->audio_format.bytes_per_sample * stream_config->channels * stream_config->samples_per_display_frame;
	
	sg->rb = ring_buffer_new(size_bytes);
	CHECK_ALLOC(sg->rb, return);
	
	sg->input_buffer = malloc(size_bytes);
	CHECK_ALLOC(sg->input_buffer, return);
	
	sg->fft_input_buffers[0] = fftwf_malloc(sizeof(float) * sg->fft_len * stream_config->channels);
	CHECK_ALLOC(sg->fft_input_buffers[0], return);
	sg->fft_input_buffers[1] = fftwf_malloc(sizeof(float) * sg->fft_len * stream_config->channels);
	CHECK_ALLOC(sg->fft_input_buffers[1], return);
	
	sg->current_fft_input_buffer_ix = 0;
	
	sg->fft_window_function_buffer = malloc(sizeof(float) * sg->fft_len);
	CHECK_ALLOC(sg->fft_window_function_buffer, return);
	create_window_function(sg->config.window_function, sg->fft_len, 1, WINDOW_SKEW, sg->fft_window_function_buffer);
	
	sg->fft_windowed_input_buffer =  malloc(sizeof(float) * sg->fft_len);
	CHECK_ALLOC(sg->fft_windowed_input_buffer, return);
	
	sg->fft_outputs = malloc(sizeof(*sg->fft_outputs) * stream_config->channels);
	CHECK_ALLOC(sg->fft_outputs, return);
	memset(sg->fft_outputs, 0, sizeof(*sg->fft_outputs) * stream_config->channels);
	sg->n_fft_channels = stream_config->channels;	
	for(uint32_t i = 0; i < sg->n_fft_channels; i++) {
		sg->fft_outputs[i] = fftwf_malloc(sizeof(fftwf_complex) * sg->fft_len);
		CHECK_ALLOC(sg->fft_outputs[i], return);
	}
	
	sg->plan = fftwf_plan_dft_r2c_1d(sg->fft_len, sg->fft_input_buffers[0], sg->fft_outputs[0], FFTW_ESTIMATE);
	CHECK_ERROR(sg->plan != NULL, return, "fftwf_plan_dft_r2c_1d() failed");
	
	// set flag and stream config
	sg->stream_config = *stream_config;	
	sg->streaming = 1;	
}

void spectrogram_enqueue_audio_data(spectrogram_t *sg, const void *data, uint32_t size) {
	if(sg->rb) {
		ring_buffer_write(sg->rb, data, size);
	}
}
