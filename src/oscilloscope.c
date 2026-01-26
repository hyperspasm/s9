//
//  oscilloscope.c
//  s9
//
//  Created by shea on 7/28/24.
//

#include "oscilloscope.h"

#include "s9.h"
#include "ring_buffer.h"
#include "draw_sdl.h"
#include "resampler.h"
#include "graticule.h"

typedef enum {
	trig_mode_normal,
	trig_mode_auto,
} oscilloscope_trigger_mode_t;

typedef struct {
	float time_per_display;	// seconds
	float amp_per_display;  // <= 2.0

	oscilloscope_trigger_mode_t trigger_mode;
	int trig_slope;
	float trig_thresh; // -1.0 to 1.0
	float trig_holdoff;	// seconds
} oscilloscope_settings_t;

static const oscilloscope_settings_t default_settings = {
	.time_per_display = .025f,
	.amp_per_display = 1.0f,

	.trigger_mode = trig_mode_normal,
	.trig_slope = 1,
	.trig_thresh = 0.01f,
	.trig_holdoff = 0.00001, // 10uS
};

typedef enum {
	trig_state_wait,		// waiting for holdoff pixels to pass
	trig_state_pre_trigger, // waiting for input signal to drop "below" threshold
	trig_state_trigger, 	// waiting for input signal to cross threshold
	trig_state_sweep,		// sweeping
} oscilloscope_trigger_state_t;

typedef struct {
	oscilloscope_trigger_state_t trigger_state;
	uint32_t holdoff_remain; // pixels (not samples)
	uint32_t x; // current x position in pixels
} oscilloscope_state_t;


typedef struct oscilloscope {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	SDL_Rect frame_rect;
	oscilloscope_config_t config;
	graticule_t *graticule;

	int streaming;
	s9_audio_stream_config_t stream_config;
	ring_buffer_t *rb;
	float *input_buffer;  // interleaved audio from ring buffer

	resampler_t *resampler;
	float *pixel_buffer;  // resampled mono samples at pixel rate
	uint32_t pixel_buffer_size;

	SDL_Texture *blur_buffer[2];
	uint32_t blur_buffer_index;

	oscilloscope_settings_t settings;
	oscilloscope_state_t state;

} oscilloscope_t;

// context for resampler source callback
typedef struct {
	oscilloscope_t *osc;
	float *src;           // current read position in input_buffer
	uint32_t frames_remaining;
	uint32_t channel;
	uint32_t n_channels;
} osc_source_ctx_t;

// resampler source callback - reads mono samples from interleaved input buffer
static uint32_t osc_resampler_source(void *context, float *out, uint32_t count) {
	osc_source_ctx_t *ctx = context;

	uint32_t read = 0;
	while (read < count && ctx->frames_remaining > 0) {
		out[read++] = ctx->src[ctx->channel];
		ctx->src += ctx->n_channels;
		ctx->frames_remaining--;
	}
	return read;
}

static void free_dsp_allocations(oscilloscope_t *osc) {

	if(osc->rb) {
		ring_buffer_free(&osc->rb);
	}

	if(osc->input_buffer) {
		free(osc->input_buffer);
		osc->input_buffer = NULL;
	}

	if(osc->pixel_buffer) {
		free(osc->pixel_buffer);
		osc->pixel_buffer = NULL;
		osc->pixel_buffer_size = 0;
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

	// create resampler with default config
	osc->resampler = resampler_new(NULL);
	CHECK_ALLOC(osc->resampler, free(osc); return NULL);

	// create graticule with default config
	osc->graticule = graticule_new(NULL);
	CHECK_ALLOC(osc->graticule, resampler_free(&osc->resampler); free(osc); return NULL);

	return osc;
}

void oscilloscope_free(oscilloscope_t **osc) {
	if(osc && *osc) {
		free_render_allocations(*osc);
		free_dsp_allocations(*osc);
		resampler_free(&(*osc)->resampler);
		graticule_free(&(*osc)->graticule);
		free(*osc);
		*osc = NULL;
	}
}

static void reset_state(oscilloscope_t *osc) {
	osc->state = (oscilloscope_state_t) {
		.trigger_state = trig_state_wait,
		.holdoff_remain = 0,
		.x = 0,
	};
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

		reset_state(osc);
	}

	return 0;
}

// trigger on pixel-rate mono samples
// returns 1 if triggered and ready to sweep, 0 otherwise
// advances *pixels_remaining and *src as samples are consumed
static int oscilloscope_trigger(oscilloscope_t *osc, uint32_t *pixels_remaining, float **src) {

	if(*pixels_remaining == 0) {
		return 0;
	}

	if(osc->state.trigger_state == trig_state_sweep) {
		return 1;  // already in sweep
	}

	// handle holdoff
	if(osc->state.trigger_state == trig_state_wait) {
		if(osc->state.holdoff_remain > 0) {
			uint32_t n = osc->state.holdoff_remain;
			if(n > *pixels_remaining) n = *pixels_remaining;

			osc->state.holdoff_remain -= n;
			*src += n;
			*pixels_remaining -= n;
		}

		if(osc->state.holdoff_remain == 0) {
			if(osc->settings.trigger_mode == trig_mode_auto) {
				osc->state.trigger_state = trig_state_sweep;
			} else {
				osc->state.trigger_state = trig_state_pre_trigger;
			}
		}
	}

	// wait for signal to go below threshold (pre-trigger)
	if(osc->state.trigger_state == trig_state_pre_trigger) {
		while(*pixels_remaining > 0) {
			float sample = **src;
			(*src)++;
			(*pixels_remaining)--;

			if(sample * osc->settings.trig_slope < osc->settings.trig_thresh) {
				osc->state.trigger_state = trig_state_trigger;
				break;
			}
		}
	}

	// wait for signal to cross threshold (trigger)
	if(osc->state.trigger_state == trig_state_trigger) {
		while(*pixels_remaining > 0) {
			float sample = **src;
			(*src)++;
			(*pixels_remaining)--;

			if(sample * osc->settings.trig_slope >= osc->settings.trig_thresh) {
				osc->state.trigger_state = trig_state_sweep;
				osc->state.x = 0;  // start new sweep
				break;
			}
		}
	}

	return (osc->state.trigger_state == trig_state_sweep) && (*pixels_remaining > 0);
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

	// process any available audio data
	uint32_t n_channels = osc->stream_config.channels;
	uint32_t bytes_per_frame = osc->stream_config.audio_format.bytes_per_sample * n_channels;
	uint32_t bytes_avail = ring_buffer_read_avail(osc->rb);
	uint32_t frames_avail = bytes_avail / bytes_per_frame;

	if(frames_avail > 0) {
		// read all available frames from ring buffer
		uint32_t bytes_to_read = frames_avail * bytes_per_frame;
		uint32_t bytes_read = ring_buffer_read(osc->rb, (uint8_t*)osc->input_buffer, bytes_to_read);
		uint32_t frames_read = bytes_read / bytes_per_frame;

		// calculate resampling ratio
		float pixel_rate = (float)osc->frame_rect.w / osc->settings.time_per_display;
		float ratio = pixel_rate / osc->stream_config.sample_rate;

		// estimate max pixels we could produce from available frames
		uint32_t max_pixels = (uint32_t)(frames_read * ratio) + 1;

		// ensure pixel buffer is large enough
		if(osc->pixel_buffer == NULL || osc->pixel_buffer_size < max_pixels) {
			free(osc->pixel_buffer);
			osc->pixel_buffer_size = max_pixels * 2;
			osc->pixel_buffer = malloc(osc->pixel_buffer_size * sizeof(float));
		}

		if(osc->pixel_buffer) {
			// set up source context for resampler
			osc_source_ctx_t source_ctx = {
				.osc = osc,
				.src = osc->input_buffer,
				.frames_remaining = frames_read,
				.channel = 0,
				.n_channels = n_channels,
			};

			// resample to pixel rate
			uint32_t pixels_produced = resampler_read(
				osc->resampler,
				osc->pixel_buffer,
				max_pixels,
				ratio,
				osc_resampler_source,
				&source_ctx
			);

			if(pixels_produced > 0) {
				// convert holdoff from seconds to pixels
				float pixel_rate = (float)osc->frame_rect.w / osc->settings.time_per_display;
				uint32_t holdoff_pixels = (uint32_t)(osc->settings.trig_holdoff * pixel_rate);

				// process pixel-rate samples
				float *src = osc->pixel_buffer;
				uint32_t pixels_remaining = pixels_produced;

				float vertical_factor = osc->frame_rect.h / osc->settings.amp_per_display * -1.f;
				float vertical_offset = osc->frame_rect.y + osc->frame_rect.h * 0.5f;

				while(pixels_remaining > 0) {
					if(oscilloscope_trigger(osc, &pixels_remaining, &src)) {
						// we're in sweep mode - determine how many pixels to draw
						uint32_t pixels_to_draw = osc->frame_rect.w - osc->state.x;
						if(pixels_to_draw > pixels_remaining) {
							pixels_to_draw = pixels_remaining;
						}

						if(pixels_to_draw < 2) {
							// not enough for a path - consume what we have and advance state
							src += pixels_to_draw;
							pixels_remaining -= pixels_to_draw;
							osc->state.x += pixels_to_draw;

							// check if sweep complete
							if(osc->state.x >= (uint32_t)osc->frame_rect.w) {
								osc->state.trigger_state = trig_state_wait;
								osc->state.holdoff_remain = holdoff_pixels;
								osc->state.x = 0;
							}
							continue;
						}

						// create path points
						SDL_FPoint *points = malloc(sizeof(SDL_FPoint) * pixels_to_draw);
						if(!points) break;

						for(uint32_t i = 0; i < pixels_to_draw; i++) {
							float sample = *src++;
							pixels_remaining--;

							points[i].x = osc->frame_rect.x + osc->state.x;
							points[i].y = vertical_offset + sample * vertical_factor;

							osc->state.x++;
						}

						// render path
						sdl_path_t *path = sdl_path_new(&osc->config.trace_color, osc->config.trace_width, 1, points, pixels_to_draw);
						free(points);

						if(path) {
							sdl_path_render(path, renderer);
							sdl_path_free(&path);
						}

						// check if sweep complete
						if(osc->state.x >= (uint32_t)osc->frame_rect.w) {
							osc->state.trigger_state = trig_state_wait;
							osc->state.holdoff_remain = holdoff_pixels;
							osc->state.x = 0;
						}
					}
				}
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

	// allocate for streaming

	// compute number of bytes necessary to store 10 video frames of audio data
	uint32_t size_bytes = 10;
	size_bytes *= stream_config->audio_format.bytes_per_sample * stream_config->channels * stream_config->samples_per_display_frame;

	osc->rb = ring_buffer_new(size_bytes);
	CHECK_ALLOC(osc->rb, return);

	osc->input_buffer = malloc(size_bytes);
	CHECK_ALLOC(osc->input_buffer, return);

	// set flag and stream config
	osc->stream_config = *stream_config;
	osc->streaming = 1;
}

void oscilloscope_enqueue_audio_data(oscilloscope_t *osc, const void *data, uint32_t size) {
	if(osc->rb) {
		ring_buffer_write(osc->rb, data, size);
	}
}
