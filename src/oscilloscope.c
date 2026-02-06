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
	.time_per_display = .01f,
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
	uint32_t x;          // current pixel position in sweep (or pixel_buffer fill level in fast mode)
	sdl_path_t *path;    // last drawn sweep path (persists across render calls)
	uint32_t path_hold;  // frames remaining to redraw saved path when no new sweep is available

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

	if(osc->path) {
		sdl_path_free(&osc->path);
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

// build path from pixel samples, save on osc (freeing previous), and render
// pixel_buf contains n_pixels sample values; x_start is the screen x offset for the first pixel
static void build_and_render_path(oscilloscope_t *osc, SDL_Renderer *renderer,
                                  const float *pixel_buf, uint32_t n_pixels, uint32_t x_start,
                                  float vertical_factor, float vertical_offset) {
	SDL_FPoint *points = malloc(sizeof(SDL_FPoint) * n_pixels);
	if(!points) return;

	for(uint32_t i = 0; i < n_pixels; i++) {
		points[i].x = osc->frame_rect.x + x_start + i;
		points[i].y = vertical_offset + pixel_buf[i] * vertical_factor;
	}

	sdl_path_free(&osc->path);
	osc->path = sdl_path_new(&osc->config.trace_color, osc->config.trace_width, 1, points, n_pixels);
	free(points);

	if(osc->path) {
		sdl_path_render(osc->path, renderer);
	}
}

// fast mode: accumulate pixels into pixel_buffer across render calls,
// only draw when a complete sweep is available.
// osc->x tracks the fill level of pixel_buffer.
// when no new sweep is drawn, redraw the saved path for up to path_hold frames.
static void render_fast(oscilloscope_t *osc, SDL_Renderer *renderer,
                        float pixel_rate, float gain, uint32_t sweep_width, uint32_t n_frames,
                        float vertical_factor, float vertical_offset) {
	int drew = 0;
	int triggered = 0;

	while(1) {
		uint32_t pixels = osc_sampler_read_pixels(
			osc->sampler, osc->pixel_buffer + osc->x,
			sweep_width - osc->x, pixel_rate, gain, &triggered
		);

		if(pixels == 0) break;

		osc->x += pixels;

		if(osc->x >= sweep_width) {
			build_and_render_path(osc, renderer, osc->pixel_buffer,
			                      sweep_width, 0, vertical_factor, vertical_offset);
			drew = 1;
			osc->path_hold = n_frames;
			osc_sampler_sweep_complete(osc->sampler);
			osc->x = 0;
		}
	}

	// if triggered but no new sweep drawn, redraw saved path while hold remains
	if(triggered && !drew && osc->path && osc->path_hold > 0) {
		sdl_path_render(osc->path, renderer);
		osc->path_hold--;
	}
}

// slow mode: draw pixel data incrementally as it arrives.
// osc->x tracks the current screen x position within the sweep.
// pixel_buffer is used as scratch space (always written from index 0).
static void render_slow(oscilloscope_t *osc, SDL_Renderer *renderer,
                        float pixel_rate, float gain, uint32_t sweep_width,
                        float vertical_factor, float vertical_offset) {
	while(1) {
		uint32_t pixels = osc_sampler_read_pixels(
			osc->sampler, osc->pixel_buffer,
			sweep_width - osc->x, pixel_rate, gain, NULL
		);

		if(pixels == 0) break;

		if(pixels >= 2) {
			build_and_render_path(osc, renderer, osc->pixel_buffer,
			                      pixels, osc->x, vertical_factor, vertical_offset);
		}

		osc->x += pixels;

		if(osc->x >= sweep_width) {
			osc_sampler_sweep_complete(osc->sampler);
			osc->x = 0;
		}
	}
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

	if(osc->pixel_buffer && osc->sampler) {
		float pixel_rate = (float)osc->frame_rect.w / osc->settings.time_per_display;
		float gain = 1.0f / osc->settings.amp_per_display * 2.0f;
		uint32_t sweep_width = (uint32_t)osc->frame_rect.w;
		float vertical_factor = osc->frame_rect.h * 0.5f * -1.f;
		float vertical_offset = osc->frame_rect.y + osc->frame_rect.h * 0.5f;

		// fast mode threshold: round up to integer multiple of display frame period
		const float fast_mode_min_time = 0.1f;
		float frame_period = (float)osc->stream_config.samples_per_display_frame / osc->stream_config.sample_rate;
		uint32_t n_frames = (uint32_t)(fast_mode_min_time / frame_period);
		if(n_frames * frame_period < fast_mode_min_time) n_frames++;
		float fast_mode_threshold = n_frames * frame_period;

		if(osc->settings.time_per_display < fast_mode_threshold) {
			render_fast(osc, renderer, pixel_rate, gain, sweep_width, n_frames,
			            vertical_factor, vertical_offset);
		} else {
			render_slow(osc, renderer, pixel_rate, gain, sweep_width,
			            vertical_factor, vertical_offset);
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
