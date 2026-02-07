#include "lissajous.h"

#include "s9.h"
#include "ring_buffer.h"
#include "draw_sdl.h"
#include "graticule.h"

#include <math.h>

typedef struct lissajous {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	SDL_Rect frame_rect;
	lissajous_config_t config;
	graticule_t *graticule;

	int streaming;
	s9_audio_stream_config_t stream_config;
	ring_buffer_t *rb;

	SDL_Texture *blur_buffer[2];
	uint32_t blur_buffer_index;

	float *sample_buffer;        // temp buffer for dequeued samples
	uint32_t sample_buffer_size; // size in frames

} lissajous_t;

static void free_dsp_allocations(lissajous_t *liss) {
	if(liss->rb) {
		ring_buffer_free(&liss->rb);
	}

	if(liss->sample_buffer) {
		free(liss->sample_buffer);
		liss->sample_buffer = NULL;
		liss->sample_buffer_size = 0;
	}
}

static void free_render_allocations(lissajous_t *liss) {
	if(liss->blur_buffer[0]) {
		SDL_DestroyTexture(liss->blur_buffer[0]);
		liss->blur_buffer[0] = NULL;
	}

	if(liss->blur_buffer[1]) {
		SDL_DestroyTexture(liss->blur_buffer[1]);
		liss->blur_buffer[1] = NULL;
	}
}

void lissajous_set_config(lissajous_t *liss, const lissajous_config_t *config) {
	if (config == NULL) {
		liss->config = default_lissajous_config;
	} else {
		liss->config = *config;
	}
}

lissajous_t *lissajous_new(const lissajous_config_t *config) {
	lissajous_t *liss = malloc(sizeof(lissajous_t));
	CHECK_ALLOC(liss, return NULL);
	memset(liss, 0, sizeof(*liss));
	lissajous_set_config(liss, config);

	// create graticule with default config
	liss->graticule = graticule_new(NULL);
	CHECK_ALLOC(liss->graticule, free(liss); return NULL);

	return liss;
}

void lissajous_free(lissajous_t **liss) {
	if(liss && *liss) {
		free_render_allocations(*liss);
		free_dsp_allocations(*liss);
		graticule_free(&(*liss)->graticule);
		free(*liss);
		*liss = NULL;
	}
}

static S9_FORCE_INLINE int prepare_to_draw(lissajous_t *liss, SDL_Renderer *renderer) {
	SDL_Rect viewport;
	SDL_RenderGetViewport(renderer, &viewport);

	if(liss->renderer != renderer || !SDL_RectEquals(&liss->viewport, &viewport)) {
		liss->renderer = renderer;
		liss->viewport = viewport;

		free_render_allocations(liss);

		liss->blur_buffer[0] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, liss->viewport.w, liss->viewport.h);
		CHECK_ALLOC(liss->blur_buffer[0], return -1);
		SDL_SetRenderTarget(renderer, liss->blur_buffer[0]);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
		SDL_RenderClear(renderer);

		liss->blur_buffer[1] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, liss->viewport.w, liss->viewport.h);
		SDL_SetRenderTarget(renderer, liss->blur_buffer[1]);
		CHECK_ALLOC(liss->blur_buffer[1], return -1);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
		SDL_RenderClear(renderer);

		liss->blur_buffer_index = 0;

		liss->frame_rect = (SDL_Rect){ .x = 10, .y = 10, .w = viewport.w - 20, .h = viewport.h - 20 };
	}

	return 0;
}

void lissajous_render(lissajous_t *liss, SDL_Renderer *renderer) {
	if(!liss->streaming) return;
	if(prepare_to_draw(liss, renderer)) return;

	// check if we have at least 2 channels
	if(liss->stream_config.channels < 2) return;

	// select and toggle blur buffer (always, for smooth persistence)
	SDL_Texture *prv = liss->blur_buffer[liss->blur_buffer_index];
	liss->blur_buffer_index = liss->blur_buffer_index == 0;
	SDL_Texture *cur = liss->blur_buffer[liss->blur_buffer_index];

	// draw to current blur_buffer with clipping to frame_rect
	SDL_SetRenderTarget(renderer, cur);
	SDL_RenderSetClipRect(renderer, &liss->frame_rect);
	draw_sdl_clear_viewport(renderer, NULL);

	// drain ring buffer and draw samples
	if(liss->rb && liss->sample_buffer) {
		uint32_t n_channels = liss->stream_config.channels;
		uint32_t bytes_per_frame = liss->stream_config.audio_format.bytes_per_sample * n_channels;
		uint32_t bytes_avail = ring_buffer_read_avail(liss->rb);
		uint32_t frames_avail = bytes_avail / bytes_per_frame;

		// limit to sample buffer size
		if(frames_avail > liss->sample_buffer_size) {
			frames_avail = liss->sample_buffer_size;
		}

		if(frames_avail > 0) {
			uint32_t bytes_to_read = frames_avail * bytes_per_frame;
			uint32_t bytes_read = ring_buffer_read(liss->rb, (uint8_t*)liss->sample_buffer, bytes_to_read);
			S9_ASSERT(bytes_read == bytes_to_read);

			// build path from samples (channel 0 = X, channel 1 = Y)
			if(frames_avail >= 2) {
				SDL_FPoint *points = malloc(sizeof(SDL_FPoint) * frames_avail);
				if(points) {
					float center_x = liss->frame_rect.x + liss->frame_rect.w * 0.5f;
					float center_y = liss->frame_rect.y + liss->frame_rect.h * 0.5f;
					float scale_x = liss->frame_rect.w * 0.5f / liss->config.gain;
					float scale_y = liss->frame_rect.h * 0.5f / liss->config.gain * -1.0f; // flip Y axis

					for(uint32_t i = 0; i < frames_avail; i++) {
						float x = liss->sample_buffer[i * n_channels + 0];  // channel 0 = X
						float y = liss->sample_buffer[i * n_channels + 1];  // channel 1 = Y
						points[i].x = center_x + x * scale_x;
						points[i].y = center_y + y * scale_y;
					}

					sdl_path_t *path = sdl_path_new(&liss->config.trace_color, liss->config.trace_width, 0, points, frames_avail);
					free(points);

					if(path) {
						sdl_path_render(path, renderer);
						sdl_path_free(&path);
					}
				}
			}
		}
	}

	// clear clip rect before blending
	SDL_RenderSetClipRect(renderer, NULL);

	// blend in history blur_buffer for persistence effect (always, even if no new data)
	SDL_SetTextureBlendMode(prv, SDL_BLENDMODE_ADD);
	SDL_SetTextureAlphaMod(prv, liss->config.persistence);
	SDL_RenderCopy(renderer, prv, NULL, NULL);

	// blit current blur_buffer to screen
	SDL_SetRenderTarget(renderer, NULL);
	SDL_SetTextureBlendMode(cur, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, cur, NULL, NULL);

	// render graticule on top
	graticule_render(liss->graticule, renderer, &liss->frame_rect);
}

void lissajous_prepare_to_stream(lissajous_t *liss, const s9_audio_stream_config_t *stream_config) {
	// only S9_AUDIO_FLOAT32 is supported
	CHECK_PARAMETER(stream_config->audio_format.sample_type == S9_SAMPLE_FLOAT32, return);

	if( S9_AUDIO_STREAM_CONFIG_EQUALS(liss->stream_config, *stream_config) ) {
		// no change
		return;
	}

	// free dsp allocations
	free_dsp_allocations(liss);

	// create ring buffer with capacity for 1 second of data
	uint32_t buffer_frames = stream_config->sample_rate;
	uint32_t buffer_bytes = buffer_frames * stream_config->audio_format.bytes_per_sample * stream_config->channels;
	liss->rb = ring_buffer_new(buffer_bytes);
	CHECK_ALLOC(liss->rb, return);

	// allocate sample buffer for 1 second of frames
	liss->sample_buffer_size = buffer_frames;
	liss->sample_buffer = malloc(liss->sample_buffer_size * stream_config->audio_format.bytes_per_sample * stream_config->channels);
	CHECK_ALLOC(liss->sample_buffer, return);

	// set flag and stream config
	liss->stream_config = *stream_config;
	liss->streaming = 1;
}

void lissajous_enqueue_audio_data(lissajous_t *liss, const void *data, uint32_t size) {
	if(liss->rb) {
		ring_buffer_write(liss->rb, data, size);
	}
}
