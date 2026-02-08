#include "lissajous.h"

#include "s9.h"
#include "resampler.h"
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
	resampler_t *resampler;

	SDL_Texture *blur_buffer[2];
	uint32_t blur_buffer_index;

	float *resampled_buffer;     // temp buffer for resampled interleaved data
	SDL_FPoint *points;          // points buffer for drawing
	uint32_t max_points;         // capacity in points (1 second of pixel_rate)

} lissajous_t;

static void free_dsp_allocations(lissajous_t *liss) {
	if(liss->resampler) {
		resampler_free(&liss->resampler);
	}

	if(liss->resampled_buffer) {
		free(liss->resampled_buffer);
		liss->resampled_buffer = NULL;
	}

	if(liss->points) {
		free(liss->points);
		liss->points = NULL;
	}

	liss->max_points = 0;
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

	// read resampled data and draw
	if(liss->resampler && liss->resampled_buffer && liss->points) {
		// calculate resampling rate (pixel_rate / sample_rate)
		float rate = liss->config.pixel_rate / (float)liss->stream_config.sample_rate;

		// read resampled frames (up to max_points)
		uint32_t frames = resampler_read(liss->resampler, liss->resampled_buffer,
		                                  liss->max_points, rate, liss->config.gain);

		if(frames >= 2) {
			float center_x = liss->frame_rect.x + liss->frame_rect.w * 0.5f;
			float center_y = liss->frame_rect.y + liss->frame_rect.h * 0.5f;
			float scale_x = liss->frame_rect.w * 0.5f;
			float scale_y = liss->frame_rect.h * 0.5f * -1.0f; // flip Y axis

			// convert interleaved stereo to points (channel 0 = X, channel 1 = Y)
			for(uint32_t i = 0; i < frames; i++) {
				float x = liss->resampled_buffer[i * 2 + 0];  // channel 0 = X
				float y = liss->resampled_buffer[i * 2 + 1];  // channel 1 = Y
				liss->points[i].x = center_x + x * scale_x;
				liss->points[i].y = center_y + y * scale_y;
			}

			sdl_path_t *path = sdl_path_new(&liss->config.trace_color, liss->config.trace_width, 0, liss->points, frames);
			if(path) {
				sdl_path_render(path, renderer);
				sdl_path_free(&path);
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

	// create resampler with 2 channels, 1 second buffer
	uint32_t buffer_frames = stream_config->sample_rate;
	liss->resampler = resampler_new(2, buffer_frames);
	CHECK_ALLOC(liss->resampler, return);

	// allocate buffers for 1 second of pixels (based on pixel_rate)
	liss->max_points = (uint32_t)liss->config.pixel_rate;

	// resampled buffer: interleaved stereo (2 channels)
	liss->resampled_buffer = malloc(liss->max_points * 2 * sizeof(float));
	CHECK_ALLOC(liss->resampled_buffer, return);

	// points buffer for drawing
	liss->points = malloc(liss->max_points * sizeof(SDL_FPoint));
	CHECK_ALLOC(liss->points, return);

	// set flag and stream config
	liss->stream_config = *stream_config;
	liss->streaming = 1;
}

void lissajous_enqueue_audio_data(lissajous_t *liss, const void *data, uint32_t size) {
	if(liss->resampler) {
		uint32_t n_channels = liss->stream_config.channels;
		uint32_t bytes_per_frame = liss->stream_config.audio_format.bytes_per_sample * n_channels;
		uint32_t frames = size / bytes_per_frame;
		resampler_enqueue(liss->resampler, (const float *)data, frames, n_channels);
	}
}
