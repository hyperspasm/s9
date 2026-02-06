//
//  oscilloscope.c
//  s9
//
//  Created by shea on 7/28/24.
//

#include "oscilloscope.h"

#include "s9.h"
#include "osc_sampler.h"
#include "draw_sdl.h"
#include "graticule.h"

typedef struct {
	float time_per_display;	// seconds
	float amp_per_display;  // <= 2.0
} oscilloscope_settings_t;

static const oscilloscope_settings_t default_settings = {
	.time_per_display = .025f,
	.amp_per_display = 2.0f,
};

typedef struct oscilloscope {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	SDL_Rect frame_rect;
	oscilloscope_config_t config;
	graticule_t *graticule;

	int streaming;
	s9_audio_stream_config_t stream_config;
	osc_sampler_t *sampler;

	float *pixel_buffer;  // output from osc_sampler_read_pixels
	uint32_t pixel_buffer_size;

	SDL_Texture *blur_buffer[2];
	uint32_t blur_buffer_index;

	oscilloscope_settings_t settings;
	uint32_t x; // current x position in pixels (sweep progress)

} oscilloscope_t;

static void free_dsp_allocations(oscilloscope_t *osc) {

	if(osc->sampler) {
		osc_sampler_free(&osc->sampler);
	}
}

static void free_render_allocations(oscilloscope_t *osc) {

	if(osc->blur_buffer[0]) {
		SDL_DestroyTexture(osc->blur_buffer[0]);
		osc->blur_buffer[0] = NULL;
	}

	if(osc->blur_buffer[1]) {
		SDL_DestroyTexture(osc->blur_buffer[1]);
		osc->blur_buffer[1] = NULL;
	}

	if(osc->pixel_buffer) {
		free(osc->pixel_buffer);
		osc->pixel_buffer = NULL;
		osc->pixel_buffer_size = 0;
	}
}

void oscilloscope_set_config(oscilloscope_t *osc, const oscilloscope_config_t *config) {
	if (config == NULL) {
		osc->config = default_oscilloscope_config;
	} else {
		osc->config = *config;
	}
}

oscilloscope_t *oscilloscope_new(const oscilloscope_config_t *config) {
	oscilloscope_t *osc = malloc(sizeof(oscilloscope_t));
	CHECK_ALLOC(osc, return NULL);
	memset(osc, 0, sizeof(*osc));
	oscilloscope_set_config(osc, config);

	osc->settings = default_settings;

	// create graticule with default config
	osc->graticule = graticule_new(NULL);
	CHECK_ALLOC(osc->graticule, free(osc); return NULL);

	return osc;
}

void oscilloscope_free(oscilloscope_t **osc) {
	if(osc && *osc) {
		free_render_allocations(*osc);
		free_dsp_allocations(*osc);
		graticule_free(&(*osc)->graticule);
		free(*osc);
		*osc = NULL;
	}
}

static S9_FORCE_INLINE int prepare_to_draw(oscilloscope_t *osc, SDL_Renderer *renderer) {
	SDL_Rect viewport;
	SDL_RenderGetViewport(renderer, &viewport);

	if(osc->renderer != renderer || !SDL_RectEquals(&osc->viewport, &viewport)) {
		osc->renderer = renderer;
		osc->viewport = viewport;

		free_render_allocations(osc);

		osc->blur_buffer[0] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, osc->viewport.w, osc->viewport.h);
		CHECK_ALLOC(osc->blur_buffer[0], return -1);
		SDL_SetRenderTarget(renderer, osc->blur_buffer[0]);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
		SDL_RenderClear(renderer);

		osc->blur_buffer[1] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, osc->viewport.w, osc->viewport.h);
		SDL_SetRenderTarget(renderer, osc->blur_buffer[1]);
		CHECK_ALLOC(osc->blur_buffer[1], return -1);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
		SDL_RenderClear(renderer);

		osc->blur_buffer_index = 0;

		osc->frame_rect = (SDL_Rect){ .x = 10, .y = 10, .w = viewport.w - 20, .h = viewport.h - 20 };

		free(osc->pixel_buffer);
		osc->pixel_buffer_size = (uint32_t)osc->frame_rect.w;
		osc->pixel_buffer = malloc(osc->pixel_buffer_size * sizeof(float));

		osc->x = 0;
	}

	return 0;
}

void oscilloscope_render(oscilloscope_t *osc, SDL_Renderer *renderer) {
	if(!osc->streaming) return;
	if(prepare_to_draw(osc, renderer)) return;

	// select and toggle blur buffer (always, for smooth persistence)
	SDL_Texture *prv = osc->blur_buffer[osc->blur_buffer_index];
	osc->blur_buffer_index = osc->blur_buffer_index == 0;
	SDL_Texture *cur = osc->blur_buffer[osc->blur_buffer_index];

	// draw to current blur_buffer with clipping to frame_rect
	SDL_SetRenderTarget(renderer, cur);
	SDL_RenderSetClipRect(renderer, &osc->frame_rect);
	draw_sdl_clear_viewport(renderer, NULL);

	// compute display parameters
	float pixel_rate = (float)osc->frame_rect.w / osc->settings.time_per_display;
	float gain = 1.0f / osc->settings.amp_per_display * 2.0f;
	uint32_t sweep_width = (uint32_t)osc->frame_rect.w;

	if(osc->pixel_buffer) {
		float vertical_factor = osc->frame_rect.h * 0.5f * -1.f;
		float vertical_offset = osc->frame_rect.y + osc->frame_rect.h * 0.5f;

		// read and draw pixel data (may span multiple sweeps)
		while(1) {
			uint32_t pixels_wanted = sweep_width - osc->x;
			uint32_t pixels = osc_sampler_read_pixels(
				osc->sampler, osc->pixel_buffer, pixels_wanted, pixel_rate, gain
			);

			if(pixels == 0) break;

			if(pixels >= 2) {
				// create path points
				SDL_FPoint *points = malloc(sizeof(SDL_FPoint) * pixels);
				if(!points) break;

				for(uint32_t i = 0; i < pixels; i++) {
					points[i].x = osc->frame_rect.x + osc->x;
					points[i].y = vertical_offset + osc->pixel_buffer[i] * vertical_factor;
					osc->x++;
				}

				// render path
				sdl_path_t *path = sdl_path_new(&osc->config.trace_color, osc->config.trace_width, 1, points, pixels);
				free(points);

				if(path) {
					sdl_path_render(path, renderer);
					sdl_path_free(&path);
				}
			} else {
				// not enough for a path - just advance
				osc->x += pixels;
			}

			// check if sweep complete
			if(osc->x >= sweep_width) {
				osc_sampler_sweep_complete(osc->sampler);
				osc->x = 0;
			}
		}
	}

	// clear clip rect before blending
	SDL_RenderSetClipRect(renderer, NULL);

	// blend in history blur_buffer for persistence effect (always, even if no new data)
	SDL_SetTextureBlendMode(prv, SDL_BLENDMODE_ADD);
	SDL_SetTextureAlphaMod(prv, osc->config.persistence);
	SDL_RenderCopy(renderer, prv, NULL, NULL);

	// blit current blur_buffer to screen
	SDL_SetRenderTarget(renderer, NULL);
	SDL_SetTextureBlendMode(cur, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, cur, NULL, NULL);

	// render graticule on top
	graticule_render(osc->graticule, renderer, &osc->frame_rect);
}


void oscilloscope_prepare_to_stream(oscilloscope_t *osc, const s9_audio_stream_config_t *stream_config) {
	// only S9_AUDIO_FLOAT32 is supported
	CHECK_PARAMETER(stream_config->audio_format.sample_type == S9_SAMPLE_FLOAT32, return);

	if( S9_AUDIO_STREAM_CONFIG_EQUALS(osc->stream_config, *stream_config) ) {
		// no change
		return;
	}

	// free dsp allocations
	free_dsp_allocations(osc);

	// create sampler with capacity for 10 video frames of mono samples
	uint32_t buffer_size = 10 * stream_config->samples_per_display_frame;
	osc->sampler = osc_sampler_new(stream_config->sample_rate, buffer_size);
	CHECK_ALLOC(osc->sampler, return);

	// set flag and stream config
	osc->stream_config = *stream_config;
	osc->streaming = 1;
}

void oscilloscope_enqueue_audio_data(oscilloscope_t *osc, const void *data, uint32_t size) {
	if(osc->sampler) {
		uint32_t n_channels = osc->stream_config.channels;
		uint32_t bytes_per_frame = osc->stream_config.audio_format.bytes_per_sample * n_channels;
		uint32_t frames = size / bytes_per_frame;
		osc_sampler_enqueue(osc->sampler, (const float *)data, frames, n_channels);
	}
}
