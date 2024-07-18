#include "vu.h"
#include "s9.h"
#include "ring_buffer.h"

typedef struct vu {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	s9_audio_stream_config_t stream_config;
	ring_buffer_t *rb;
	float *working_buffer;
} vu_t;

vu_t *vu_new(void) {
	vu_t *vu = (vu_t *) malloc(sizeof(vu_t));
	CHECK_ALLOC(vu, return NULL);
	memset(vu, 0, sizeof(*vu));
	
	// TODO
	
	return vu;
}

void vu_free(vu_t **vu) {
	if (vu && *vu) {
		
		if((*vu)->rb) {
			ring_buffer_free(&((*vu)->rb));
		}
		
		if((*vu)->working_buffer) {
			free((*vu)->working_buffer);
			(*vu)->working_buffer = NULL;
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
		
		// TODO
	}
}

void vu_render(vu_t *vu, SDL_Renderer *renderer) {
	prepare_to_draw(vu, renderer);
	
	if(vu->rb && vu->working_buffer) {
		uint32_t bytes_avail = ring_buffer_read_avail(vu->rb);
		uint32_t samples_avail = bytes_avail / (vu->stream_config.audio_format.bytes_per_sample * vu->stream_config.channels);		
		
		uint32_t samples_to_consume = samples_avail;
		if(samples_avail > vu->stream_config.samples_per_display_frame) {
			samples_to_consume = (samples_avail / vu->stream_config.samples_per_display_frame) * vu->stream_config.samples_per_display_frame;
			//samples_to_consume = vu->stream_config.samples_per_display_frame + (samples_avail - vu->stream_config.samples_per_display_frame) /2;
		}
		uint32_t bytes_to_consume = samples_to_consume * vu->stream_config.audio_format.bytes_per_sample * vu->stream_config.channels;		
		ring_buffer_read(vu->rb, (uint8_t*)vu->working_buffer, bytes_to_consume);

		
		// TODO
		printf("%d\n", samples_to_consume);
	}
}

void vu_prepare_to_stream(vu_t *vu, const s9_audio_stream_config_t *stream_config) {
	// only S9_AUDIO_FLOAT32 is supported
	CHECK_PARAMETER(stream_config->audio_format.sample_type == S9_SAMPLE_FLOAT32, return);
	
	if( S9_AUDIO_STREAM_CONFIG_EQUALS(vu->stream_config, *stream_config) ) {
		// no change
		return;
	}
	
	if(vu->rb) {
		ring_buffer_free(&vu->rb);
	}
	
	if(vu->working_buffer) {
		free(vu->working_buffer);
		vu->working_buffer = NULL;
	}
	
	vu->stream_config.audio_format.sample_type = S9_SAMPLE_NONE;
	
	// compute number of bytes necessary to store 10 video frames of audio data
	uint32_t size_bytes = 10;
	size_bytes *= stream_config->audio_format.bytes_per_sample * stream_config->channels * stream_config->samples_per_display_frame;
	
	vu->rb = ring_buffer_new(size_bytes);
	CHECK_ALLOC(vu->rb, return);
	
	vu->working_buffer = malloc(size_bytes);
	CHECK_ALLOC(vu->working_buffer, return);
	
	vu->stream_config = *stream_config;	
}

void vu_enqueue_audio_data(vu_t *vu, const void *data, uint32_t size) {
	if(vu->rb) {
		ring_buffer_write(vu->rb, data, size);
	}
}
