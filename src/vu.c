#include "vu.h"
#include "s9.h"
#include "ring_buffer.h"
#include "window_function.h"

typedef struct {
	float rms;
	float db_rms;
} channel_val_t;

typedef struct vu {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	int draw_vertical;
	
	int streaming;
	s9_audio_stream_config_t stream_config;
	ring_buffer_t *rb;
	float *input_buffer;
	
	uint32_t rms_window_frames;
	float *rms_window_function_buffer;
	float *rms_window_buffer;
	float *rms_window_buffers[2];
	int current_rms_window_buffer_ix;
	
	
	
	channel_val_t *channel_vals;
	
	
	
	
	// dsp configuration parameters
	float rms_window_seconds;
	window_function_t rms_window_function;
	
	// drawing configuration parameters
	float min_dbfs;
	int margin;
} vu_t;

vu_t *vu_new(void) {
	vu_t *vu = (vu_t *) malloc(sizeof(vu_t));
	CHECK_ALLOC(vu, return NULL);
	memset(vu, 0, sizeof(*vu));
	
	vu->rms_window_seconds = 0.25f;
	vu->min_dbfs = -60.f;
	vu->margin = 8;
	
	return vu;
}

void vu_free(vu_t **vu) {
	if (vu && *vu) {
		
		if((*vu)->rb) {
			ring_buffer_free(&((*vu)->rb));
		}
		
		if((*vu)->input_buffer) {
			free((*vu)->input_buffer);
			(*vu)->input_buffer = NULL;
		}
		
		if((*vu)->rms_window_buffer) {
			free((*vu)->rms_window_buffer);
			(*vu)->rms_window_buffer = NULL;
			(*vu)->rms_window_buffers[0] = NULL;
			(*vu)->rms_window_buffers[1] = NULL;
			(*vu)->current_rms_window_buffer_ix = 0;
		}
		
		if((*vu)->rms_window_function_buffer) {
			free((*vu)->rms_window_function_buffer);
			(*vu)->rms_window_function_buffer = NULL;
		}
		
		if((*vu)->channel_vals) {
			free((*vu)->channel_vals);
			(*vu)->channel_vals = NULL;
		}
		
		free(*vu);
		*vu = NULL;
	}
}

static S9_FORCE_INLINE void prepare_to_draw(vu_t *vu, SDL_Renderer *renderer) {
	SDL_Rect viewport;	
	SDL_RenderGetViewport(renderer, &viewport);
	
	if(vu->renderer != renderer || !SDL_RectEquals(&vu->viewport, &viewport)) {
		vu->renderer = renderer;
		vu->viewport = viewport;		
		
		vu->draw_vertical =  vu->viewport.w < vu->viewport.h;
	}
}
#if 0
void vu_render(vu_t *vu, SDL_Renderer *renderer) {
	prepare_to_draw(vu, renderer);	
	if(!vu->streaming) return;
	
	
	uint32_t bytes_avail = ring_buffer_read_avail(vu->rb);
	uint32_t samples_avail = bytes_avail / (vu->stream_config.audio_format.bytes_per_sample * vu->stream_config.channels);		
	
	uint32_t samples_to_consume = samples_avail;
	if(samples_avail > vu->stream_config.samples_per_display_frame) {
		samples_to_consume = (samples_avail / vu->stream_config.samples_per_display_frame) * vu->stream_config.samples_per_display_frame;
		//samples_to_consume = vu->stream_config.samples_per_display_frame + (samples_avail - vu->stream_config.samples_per_display_frame) /2;
	}
	uint32_t bytes_to_consume = samples_to_consume * vu->stream_config.audio_format.bytes_per_sample * vu->stream_config.channels;		
	uint32_t bytes_consumed = ring_buffer_read(vu->rb, (uint8_t*)vu->input_buffer, bytes_to_consume);
	S9_ASSERT(bytes_consumed == bytes_to_consume);
	
	channel_val_t channel_vals[vu->stream_config.channels];
	float val[vu->stream_config.channels];
	
	// compute rms for each channel				
	for(uint32_t j=0; j < vu->stream_config.channels; j++) {
		val[j] = 0.f;
	}
	
	float *src = vu->input_buffer;
	for(uint32_t i = 0; i < samples_to_consume; i++) {
		for(uint32_t j=0; j < vu->stream_config.channels; j++) {
			val[j] += (*src) * (*src);
			src++;
		}
	}
	
	for(uint32_t j=0; j < vu->stream_config.channels; j++) {
		val[j] = val[j] / samples_to_consume;
		channel_vals[j].rms = sqrtf(val[j]);
	}
	
	// convert each channel to dB
	for(uint32_t j=0; j < vu->stream_config.channels; j++) {
		channel_vals[j].db_rms = 20.f * log10f(channel_vals[j].rms);
	}
	
	
	
	for(uint32_t j=0; j < vu->stream_config.channels; j++) {			
		if(channel_vals[j].db_rms < vu->channel_vals[j].db_rms) {
			channel_vals[j].db_rms = vu->channel_vals[j].db_rms - 0.5f;
		}
	}
	
	
	// convert each channel to normalized bar length
	for(uint32_t j=0; j < vu->stream_config.channels; j++) {
		val[j] = (channel_vals[j].db_rms - vu->min_dbfs) / (-vu->min_dbfs);			
		val[j] = val[j] < 0.f ? 0.f : val[j] > 1.f ? 1.f : val[j]; // clamp 0 - 1
	}
	
	
	
	int bar_width = (vu->viewport.w - (vu->stream_config.channels - 1) * vu->margin) / vu->stream_config.channels;
	int x = 0;
	for(uint32_t j=0; j < vu->stream_config.channels; j++) {
		int bar_height = val[j] * vu->viewport.h;
		SDL_Rect bar_rect = (SDL_Rect) {
			.x = x,
			.y = vu->viewport.h - bar_height,
			.w = bar_width,
			.h = bar_height,
		};
		
		SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
		SDL_RenderFillRect(renderer, &bar_rect);
		
		x += bar_width + vu->margin;
	}
	
	
	for(uint32_t j=0; j < vu->stream_config.channels; j++) {
		vu->channel_vals[j] = channel_vals[j];
	}
	
	
	
	
	if(vu->draw_vertical) {
		
	} else {
		
	}
}
#else
void vu_render(vu_t *vu, SDL_Renderer *renderer) {
	prepare_to_draw(vu, renderer);	
	if(!vu->streaming) return;
	
	uint32_t n_channels = vu->stream_config.channels;
	uint32_t bytes_per_frame = vu->stream_config.audio_format.bytes_per_sample * n_channels;
	uint32_t bytes_avail = ring_buffer_read_avail(vu->rb);
	S9_ASSERT(bytes_avail % vu->stream_config.audio_format.bytes_per_sample * n_channels == 0);
	uint32_t frames_avail = bytes_avail / bytes_per_frame;	
	
	// compute how many frames to consume
	uint32_t frames_to_consume = frames_avail;
	if(frames_avail > vu->stream_config.samples_per_display_frame) {
		frames_to_consume = (frames_avail / vu->stream_config.samples_per_display_frame) * vu->stream_config.samples_per_display_frame;
	}
	
	// copy interleaved data out of the ring buffer to the input buffer
	uint32_t bytes_to_dequeue = frames_to_consume * bytes_per_frame;		
	uint32_t bytes_dequeued = ring_buffer_read(vu->rb, (uint8_t*)vu->input_buffer, bytes_to_dequeue);
	S9_ASSERT(bytes_dequeued == bytes_to_dequeue);
	
	
	float *current_rms_window_buffer = vu->rms_window_buffers[vu->current_rms_window_buffer_ix];
	const float *last_rms_window_buffer = vu->rms_window_buffers[vu->current_rms_window_buffer_ix == 0];	
	vu->current_rms_window_buffer_ix = vu->current_rms_window_buffer_ix == 0; // toggle current_rms_window_buffer_ix for next time
	
	uint32_t n_history_frames = 0; // number of frames to re-use
	uint32_t n_frames_to_drop = 0; // number of frames that need to get dropped because we can't keep up (hopefully none)
	if(frames_to_consume < vu->rms_window_frames) {
		n_history_frames = vu->rms_window_frames - frames_to_consume;
	} else {
		// dropping frames  (defensive, hopefully this doesn't really happen)
		n_frames_to_drop = frames_to_consume - vu->rms_window_frames;
		frames_to_consume = vu->rms_window_frames;		  
	}
	
	// copy history
	if(n_history_frames) {
		memcpy(&current_rms_window_buffer[0], &last_rms_window_buffer[(vu->rms_window_frames - n_history_frames) * n_channels], n_history_frames * bytes_per_frame);		
	}
	
	// copy incoming frames
	if(frames_to_consume) {
		memcpy(&current_rms_window_buffer[n_history_frames * n_channels], &vu->input_buffer[n_frames_to_drop * n_channels], frames_to_consume * bytes_per_frame);
	}
	
	
	
	
	channel_val_t channel_vals[n_channels];
	float val[n_channels];
	
	
	
	// compute rms for each channel				
	for(uint32_t j=0; j < n_channels; j++) {
		val[j] = 0.f;
	}
	
#if 1
	float *src = current_rms_window_buffer;
	for(uint32_t i = 0; i < vu->rms_window_frames; i++) {
		for(uint32_t j=0; j < n_channels; j++) {
			float sample = *src++;
			sample *= vu->rms_window_function_buffer[i];
			val[j] += sample * sample;
		}
	}
	
	for(uint32_t j=0; j < n_channels; j++) {
		val[j] = val[j] / vu->rms_window_frames;
		channel_vals[j].rms = sqrtf(val[j]);
	}
#else
	
	
	
	float *src = vu->input_buffer;
	for(uint32_t i = 0; i < frames_to_consume; i++) {
		for(uint32_t j=0; j < n_channels; j++) {
			val[j] += (*src) * (*src);
			src++;
		}
	}
	
	for(uint32_t j=0; j < n_channels; j++) {
		val[j] = val[j] / frames_to_consume;
		channel_vals[j].rms = sqrtf(val[j]);
	}
#endif
	
	// convert each channel to dB
	for(uint32_t j=0; j < n_channels; j++) {
		channel_vals[j].db_rms = 20.f * log10f(channel_vals[j].rms);
	}
	
	
#if 0
	for(uint32_t j=0; j < n_channels; j++) {			
		if(channel_vals[j].db_rms < vu->channel_vals[j].db_rms) {
			channel_vals[j].db_rms = vu->channel_vals[j].db_rms - 0.5f;
		}
	}
#endif
	
	
	// convert each channel to normalized bar length
	for(uint32_t j=0; j < n_channels; j++) {
		val[j] = (channel_vals[j].db_rms - vu->min_dbfs) / (-vu->min_dbfs);			
		val[j] = val[j] < 0.f ? 0.f : val[j] > 1.f ? 1.f : val[j]; // clamp 0 - 1
	}
	
	
	
	int bar_width = (vu->viewport.w - (n_channels - 1) * vu->margin) / n_channels;
	int x = 0;
	for(uint32_t j=0; j < n_channels; j++) {
		int bar_height = val[j] * vu->viewport.h;
		SDL_Rect bar_rect = (SDL_Rect) {
			.x = x,
			.y = vu->viewport.h - bar_height,
			.w = bar_width,
			.h = bar_height,
		};
		
		SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
		SDL_RenderFillRect(renderer, &bar_rect);
		
		x += bar_width + vu->margin;
	}
	
	
	for(uint32_t j=0; j < n_channels; j++) {
		vu->channel_vals[j] = channel_vals[j];
	}
	
	
	
	
	if(vu->draw_vertical) {
		
	} else {
		
	}
}
#endif

void vu_prepare_to_stream(vu_t *vu, const s9_audio_stream_config_t *stream_config) {
	// only S9_AUDIO_FLOAT32 is supported
	CHECK_PARAMETER(stream_config->audio_format.sample_type == S9_SAMPLE_FLOAT32, return);
	
	if( S9_AUDIO_STREAM_CONFIG_EQUALS(vu->stream_config, *stream_config) ) {
		// no change
		return;
	}
	
	// clear flag and stream config
	vu->streaming = 0;
	vu->stream_config.audio_format.sample_type = S9_SAMPLE_NONE;
	
	// free allocations
	if(vu->rb) {
		ring_buffer_free(&vu->rb);
	}
	
	if(vu->input_buffer) {
		free(vu->input_buffer);
		vu->input_buffer = NULL;
	}
	
	if(vu->rms_window_buffer) {
		free(vu->rms_window_buffer);
		vu->rms_window_buffer = NULL;
		vu->rms_window_buffers[0] = NULL;
		vu->rms_window_buffers[1] = NULL;
		vu->current_rms_window_buffer_ix = 0;
	}
	
	vu->rms_window_frames = 0;
	
	if(vu->rms_window_function_buffer) {
		free(vu->rms_window_function_buffer);
		vu->rms_window_function_buffer = NULL;
	}
	
	if(vu->channel_vals) {
		free(vu->channel_vals);
		vu->channel_vals = NULL;
	}
	
	// allocate for streaming
	
	// compute number of bytes necessary to store 10 video frames of audio data
	uint32_t size_bytes = 10;
	size_bytes *= stream_config->audio_format.bytes_per_sample * stream_config->channels * stream_config->samples_per_display_frame;
	
	vu->rb = ring_buffer_new(size_bytes);
	CHECK_ALLOC(vu->rb, return);
	
	vu->input_buffer = malloc(size_bytes);
	CHECK_ALLOC(vu->input_buffer, return);
	
	vu->rms_window_frames = ceil(stream_config->sample_rate * vu->rms_window_seconds);
	vu->rms_window_buffer = malloc(2 * vu->rms_window_frames * stream_config->channels * stream_config->audio_format.bytes_per_sample); // allocate for both rms_window_buffers
	CHECK_ALLOC(vu->rms_window_buffer, return);
	vu->rms_window_buffers[0] = &vu->rms_window_buffer[0];
	vu->rms_window_buffers[1] = &vu->rms_window_buffer[vu->rms_window_frames * stream_config->channels];
	vu->current_rms_window_buffer_ix = 0;
	
	vu->rms_window_function_buffer = malloc(vu->rms_window_frames * stream_config->audio_format.bytes_per_sample);
	CHECK_ALLOC(vu->rms_window_function_buffer, return);
	create_window_function(vu->rms_window_function, vu->rms_window_frames, vu->rms_window_function_buffer);
	
	// normalize window function buffer
	float window_function_factor = 0;
	for(uint32_t i=0; i < vu->rms_window_frames; i++) {
		window_function_factor += vu->rms_window_function_buffer[i];
	}
	window_function_factor = vu->rms_window_frames / window_function_factor;
	for(uint32_t i=0; i < vu->rms_window_frames; i++) {
		vu->rms_window_function_buffer[i] *= window_function_factor;
	}
	
	
	vu->channel_vals = malloc(sizeof(channel_val_t) * stream_config->channels);
	CHECK_ALLOC(vu->channel_vals, return);
	
	// set flag and stream config
	vu->stream_config = *stream_config;	
	vu->streaming = 1;
}

void vu_enqueue_audio_data(vu_t *vu, const void *data, uint32_t size) {
	if(vu->rb) {
		ring_buffer_write(vu->rb, data, size);
	}
}
