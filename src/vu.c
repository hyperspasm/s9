#include "vu.h"
#include "s9.h"
#include "draw_sdl.h"
#include "ring_buffer.h"

typedef struct {
	float rms;
	float db_rms;
	float peak;
	float db_peak;
} channel_val_t;

typedef struct {
	float db_peak;
	uint32_t t0;
} peak_hold_t;

typedef struct vu {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	int draw_vertical;
	
	int streaming;
	s9_audio_stream_config_t stream_config;
	ring_buffer_t *rb;
	float *input_buffer;
	channel_val_t *channel_vals;	
	peak_hold_t *peak_holds;	
	
	uint32_t rms_window_frames;
	float *rms_window_function_buffer;
	float *rms_window_buffer;
	float *rms_window_buffers[2];
	int current_rms_window_buffer_ix;
	
	vu_config_t config;
} vu_t;

vu_t *vu_new(const vu_config_t *config) {
	vu_t *vu = (vu_t *) malloc(sizeof(vu_t));
	CHECK_ALLOC(vu, return NULL);
	memset(vu, 0, sizeof(*vu));
	
	if (config == NULL) {
		vu->config = default_vu_config;
	} else {
		vu->config = *config;
	}
	
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
		
		if((*vu)->peak_holds) {
			free((*vu)->peak_holds);
			(*vu)->peak_holds = NULL;
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

static void draw(vu_t *vu, SDL_Renderer *renderer) {
	
	uint32_t n_channels = vu->stream_config.channels;
	int bar_width = (vu->viewport.w - (n_channels - 1) * vu->config.margin) / n_channels;
	
	// update peak holds
	for(int j = 0; j < n_channels; j++) {
		uint32_t now = SDL_GetTicks();
		if(vu->channel_vals[j].db_peak > vu->peak_holds[j].db_peak || (now - vu->peak_holds[j].t0) > vu->config.peak_hold_ms) {
			vu->peak_holds[j].db_peak = vu->channel_vals[j].db_peak;
			vu->peak_holds[j].t0 = now;
		}
	}
	
	// draw bar background
	for(uint32_t j = 0, x = 0; j < n_channels; j++) {		
		SDL_Rect bar_rect = (SDL_Rect) {
			.x = x,
			.y = 0,
			.w = bar_width,
			.h = vu->viewport.h,
		};
		
		SDL_SET_RENDER_DRAW_COLOR(renderer, &vu->config.bar_background_color);
		SDL_RenderFillRect(renderer, &bar_rect);
		
		x += bar_width + vu->config.margin;
	}
	
	// draw rms
	for(uint32_t j = 0, x = 0; j < n_channels; j++) {
		float top = vu->channel_vals[j].db_rms;
		top = (top - vu->config.min_dbfs) / (-vu->config.min_dbfs);			
		top = top < 0.f ? 0.f : top > 1.f ? 1.f : top; // clamp 0 - 1
		top *= vu->viewport.h;
		
		SDL_Rect bar_rect = (SDL_Rect) {
			.x = x,
			.y = vu->viewport.h - top,
			.w = bar_width,
			.h = top,
		};
		
		SDL_SET_RENDER_DRAW_COLOR(renderer, &vu->config.rms_color);
		SDL_RenderFillRect(renderer, &bar_rect);
		
		x += bar_width + vu->config.margin;
	}
	
	// draw peak
	for(uint32_t j = 0, x = 0; j < n_channels; j++) {
		float top = vu->peak_holds[j].db_peak;
		top = (top - vu->config.min_dbfs) / (-vu->config.min_dbfs);			
		top = top < 0.f ? 0.f : top > 1.f ? 1.f : top; // clamp 0 - 1
		top *= vu->viewport.h;
		
		SDL_Rect bar_rect = (SDL_Rect) {
			.x = x,
			.y = vu->viewport.h - top,
			.w = bar_width,
			.h = 4,
		};
		
		if(vu->peak_holds[j].db_peak > vu->config.over_threshold_db) {
			SDL_SET_RENDER_DRAW_COLOR(renderer, &vu->config.peak_over_color);				
		} else {
			SDL_SET_RENDER_DRAW_COLOR(renderer, &vu->config.peak_hold_color);
		}
		SDL_RenderFillRect(renderer, &bar_rect);
		
		x += bar_width + vu->config.margin;
	}
	
	// draw peak hold
	for(uint32_t j = 0, x = 0; j < n_channels; j++) {
		float top = vu->channel_vals[j].db_peak;
		top = (top - vu->config.min_dbfs) / (-vu->config.min_dbfs);			
		top = top < 0.f ? 0.f : top > 1.f ? 1.f : top; // clamp 0 - 1
		top *= vu->viewport.h;
		
		SDL_Rect bar_rect = (SDL_Rect) {
			.x = x,
			.y = vu->viewport.h - top,
			.w = bar_width,
			.h = 2,
		};
		
		SDL_SET_RENDER_DRAW_COLOR(renderer, &vu->config.peak_color);
		SDL_RenderFillRect(renderer, &bar_rect);
		
		x += bar_width + vu->config.margin;
	}
	
}

static void compute_channel_vals(vu_t *vu) {
	
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
	
	
	// -- compute peak --
	{
		for(uint32_t j = 0; j < n_channels; j++) {			
			vu->channel_vals[j].peak = 0.f;
			vu->channel_vals[j].db_peak = -INFINITY;
		}		
		
		float *src = vu->input_buffer;
		for(uint32_t i = 0; i < frames_to_consume; i++) {
			for(uint32_t j = 0; j < n_channels; j++, src++) {
				float peak_sample = fabs(*src);
				if(vu->channel_vals[j].peak < peak_sample) {
					vu->channel_vals[j].peak = peak_sample;
				}
			}
		}
		
		// compute dB values
		for(uint32_t j = 0; j < n_channels; j++) {
			vu->channel_vals[j].db_peak = 20.f * log10f(vu->channel_vals[j].peak);
		}
	}
	
	
	// -- compute rms --
	{
		// update current_rms_window_buffer for rms computation 
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
		
		// prepare to compute
		for(uint32_t j = 0; j < n_channels; j++) {			
			vu->channel_vals[j].rms = 0.f;
			vu->channel_vals[j].db_rms = -INFINITY;
		}
		
		// rms computation
		{
			float *src = current_rms_window_buffer;
			for(uint32_t i = 0; i < vu->rms_window_frames; i++) {
				for(uint32_t j = 0; j < n_channels; j++, src++) {
					float rms_sample = *src;			
					
					rms_sample *= vu->rms_window_function_buffer[i];			
					vu->channel_vals[j].rms += rms_sample * rms_sample;
				}
			}	
			for(uint32_t j = 0; j < n_channels; j++) {
				vu->channel_vals[j].rms /= vu->rms_window_frames;
				vu->channel_vals[j].rms = sqrtf(vu->channel_vals[j].rms);
			}
		}
		
		// compute dB values
		for(uint32_t j = 0; j < n_channels; j++) {
			vu->channel_vals[j].db_rms = 20.f * log10f(vu->channel_vals[j].rms);
		}
	}
	
}

void vu_render(vu_t *vu, SDL_Renderer *renderer) {
	prepare_to_draw(vu, renderer);	
	if(!vu->streaming) return;
	
	compute_channel_vals(vu);
	draw(vu, renderer);
}

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
	
	if(vu->peak_holds) {
		free(vu->peak_holds);
		vu->peak_holds = NULL;
	}
	
	// allocate for streaming
	
	// compute number of bytes necessary to store 10 video frames of audio data
	uint32_t size_bytes = 10;
	size_bytes *= stream_config->audio_format.bytes_per_sample * stream_config->channels * stream_config->samples_per_display_frame;
	
	vu->rb = ring_buffer_new(size_bytes);
	CHECK_ALLOC(vu->rb, return);
	
	vu->input_buffer = malloc(size_bytes);
	CHECK_ALLOC(vu->input_buffer, return);
	
	vu->rms_window_frames = ceil(stream_config->sample_rate * vu->config.rms_window_seconds);
	vu->rms_window_buffer = malloc(2 * vu->rms_window_frames * stream_config->channels * stream_config->audio_format.bytes_per_sample); // allocate for both rms_window_buffers
	CHECK_ALLOC(vu->rms_window_buffer, return);
	vu->rms_window_buffers[0] = &vu->rms_window_buffer[0];
	vu->rms_window_buffers[1] = &vu->rms_window_buffer[vu->rms_window_frames * stream_config->channels];
	vu->current_rms_window_buffer_ix = 0;
	
	vu->rms_window_function_buffer = malloc(vu->rms_window_frames * stream_config->audio_format.bytes_per_sample);
	CHECK_ALLOC(vu->rms_window_function_buffer, return);
	create_window_function(vu->config.rms_window_function, vu->rms_window_frames, vu->rms_window_function_buffer);
	
	// normalize window function buffer
	float window_function_factor = 0;
	for(uint32_t i = 0; i < vu->rms_window_frames; i++) {
		window_function_factor += vu->rms_window_function_buffer[i];
	}
	window_function_factor = vu->rms_window_frames / window_function_factor;
	for(uint32_t i = 0; i < vu->rms_window_frames; i++) {
		vu->rms_window_function_buffer[i] *= window_function_factor;
	}
		
	vu->channel_vals = malloc(sizeof(channel_val_t) * stream_config->channels);
	CHECK_ALLOC(vu->channel_vals, return);
	
	vu->peak_holds = malloc(sizeof(peak_hold_t) * stream_config->channels);
	CHECK_ALLOC(vu->peak_holds, return);	
	for(int j = 0; j < stream_config->channels; j++) {
		vu->peak_holds[j].db_peak = -INFINITY;
	}
	
	// set flag and stream config
	vu->stream_config = *stream_config;	
	vu->streaming = 1;
}

void vu_enqueue_audio_data(vu_t *vu, const void *data, uint32_t size) {
	if(vu->rb) {
		ring_buffer_write(vu->rb, data, size);
	}
}
