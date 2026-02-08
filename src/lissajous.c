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
	SDL_Texture *clip_texture;             // filled with trace color for MIN clipping
	SDL_BlendMode clip_blend_mode;         // MIN blend mode

	float *resampled_buffer;     // temp buffer for resampled interleaved data
	SDL_FPoint *points;          // points buffer for drawing
	uint32_t max_points;         // capacity in points (1 second of pixel_rate)

	uint32_t frames_since_draw;  // frames elapsed since last draw
	int have_last_point;         // flag: points[0] contains valid last point

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

	if(liss->clip_texture) {
		SDL_DestroyTexture(liss->clip_texture);
		liss->clip_texture = NULL;
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

	// create MIN blend mode for clipping to trace color
	// Note: disable with -DS9_DISABLE_CUSTOM_BLEND if hardware doesn't support it
#ifndef S9_DISABLE_CUSTOM_BLEND
	liss->clip_blend_mode = SDL_ComposeCustomBlendMode(
		SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MINIMUM,
		SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MINIMUM
	);
#else
	liss->clip_blend_mode = SDL_BLENDMODE_INVALID;
#endif

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

		// check if renderer supports MIN blend operation
		// OpenGL ES 1/2 don't support it, only ADD/SUBTRACT/REV_SUBTRACT
		SDL_RendererInfo info;
		if(SDL_GetRendererInfo(renderer, &info) == 0) {
			const char *name = info.name;
			if(strstr(name, "opengles") != NULL) {
				// OpenGL ES detected - disable custom blend mode
				liss->clip_blend_mode = SDL_BLENDMODE_INVALID;
			}
		}

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

		// create clip texture filled with trace color for MIN blending
		// only if custom blend mode is supported
		if(liss->clip_blend_mode != SDL_BLENDMODE_INVALID) {
			liss->clip_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, liss->viewport.w, liss->viewport.h);
			CHECK_ALLOC(liss->clip_texture, return -1);
			SDL_SetRenderTarget(renderer, liss->clip_texture);
			SDL_SetRenderDrawColor(renderer, liss->config.trace_color.r, liss->config.trace_color.g, liss->config.trace_color.b, 255);
			SDL_RenderClear(renderer);
		}

		liss->frame_rect = (SDL_Rect){ .x = 10, .y = 10, .w = viewport.w - 20, .h = viewport.h - 20 };
	}

	return 0;
}

// render path with exponential per-vertex fade to simulate phosphor decay
// oldest points (left) fade by p^K, newest points (right) are full brightness
static void render_gradient_path_lissajous(lissajous_t *liss, SDL_Renderer *renderer,
                                            const SDL_FPoint *points, uint32_t n_points) {
	if(n_points < 2) return;

	float p = liss->config.persistence / 255.0f;
	uint32_t K = liss->frames_since_draw + 1;

	uint32_t n_verts = 2 * n_points;
	uint32_t n_indices = (n_points - 1) * 6;
	float half_width = 0.5f * liss->config.trace_width;

	SDL_Vertex *vertices = malloc(n_verts * sizeof(SDL_Vertex));
	if(!vertices) return;

	int *indices = malloc(n_indices * sizeof(int));
	if(!indices) { free(vertices); return; }

	// calculate fade: left edge p^K (faded), right edge 1.0 (full brightness)
	float factor = powf(p, (float)K);
	float step = (factor > 0.0f) ? powf(factor, -1.0f / (float)(n_points - 1)) : 1.0f;

	for(uint32_t i = 0; i < n_points; i++) {
		float cx = points[i].x;
		float cy = points[i].y;

		SDL_Color c = {
			(uint8_t)(liss->config.trace_color.r * factor),
			(uint8_t)(liss->config.trace_color.g * factor),
			(uint8_t)(liss->config.trace_color.b * factor),
			255
		};

		// perpendicular vector for line width
		float dx, dy;
		if(i == 0) {
			dx = points[1].y - points[0].y;
			dy = -(points[1].x - points[0].x);
		} else if(i == n_points - 1) {
			dx = points[n_points - 1].y - points[n_points - 2].y;
			dy = -(points[n_points - 1].x - points[n_points - 2].x);
		} else {
			dx = points[i + 1].y - points[i - 1].y;
			dy = -(points[i + 1].x - points[i - 1].x);
		}

		float len = sqrtf(dx * dx + dy * dy);
		if(len > 0.0f) {
			dx = dx / len * half_width;
			dy = dy / len * half_width;
		}

		// inner edge (index i)
		vertices[i] = (SDL_Vertex){
			.position = { cx + dx, cy + dy },
			.color = c,
			.tex_coord = { 0, 0 }
		};

		// outer edge (reversed)
		vertices[n_verts - 1 - i] = (SDL_Vertex){
			.position = { cx - dx, cy - dy },
			.color = c,
			.tex_coord = { 0, 0 }
		};

		factor *= step;
	}

	// triangle strip indices
	for(uint32_t i = 0; i < n_points - 1; i++) {
		int *idx = indices + i * 6;
		idx[0] = i;
		idx[1] = n_verts - 1 - i;
		idx[2] = i + 1;
		idx[3] = n_verts - 1 - i;
		idx[4] = i + 1;
		idx[5] = n_verts - 1 - (i + 1);
	}

	SDL_RenderGeometry(renderer, NULL, vertices, n_verts, indices, n_indices);

	free(vertices);
	free(indices);
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
		// resampling rate is the upsampling factor
		float rate = liss->config.upsampling;

		// when continuing, points[0] already contains the join point
		int joining = liss->have_last_point;
		uint32_t max_frames = joining ? liss->max_points - 1 : liss->max_points;

		// read resampled frames (leaving room for join point if needed)
		uint32_t frames = resampler_read(liss->resampler, liss->resampled_buffer,
		                                  max_frames, rate, liss->config.gain);

		if(frames > 0) {
			float center_x = liss->frame_rect.x + liss->frame_rect.w * 0.5f;
			float center_y = liss->frame_rect.y + liss->frame_rect.h * 0.5f;
			float scale_x = liss->frame_rect.w * 0.5f;
			float scale_y = liss->frame_rect.h * 0.5f * -1.0f; // flip Y axis

			// destination starts after join point (if any)
			SDL_FPoint *dest = liss->points + joining;

			// convert interleaved stereo to points (channel 0 = X, channel 1 = Y)
			for(uint32_t i = 0; i < frames; i++) {
				float x = liss->resampled_buffer[i * 2 + 0];  // channel 0 = X
				float y = liss->resampled_buffer[i * 2 + 1];  // channel 1 = Y
				dest[i].x = center_x + x * scale_x;
				dest[i].y = center_y + y * scale_y;
			}

			// total points including join (if joining, points[0] already has last point)
			uint32_t total_points = frames + joining;

			// render with gradient fade (oldest -> newest)
			if(total_points >= 2) {
				render_gradient_path_lissajous(liss, renderer, liss->points, total_points);
			}

			// save new last point to points[0] for next frame
			liss->points[0] = dest[frames - 1];
			liss->have_last_point = 1;

			liss->frames_since_draw = 0;
		} else {
			liss->frames_since_draw++;
		}
	} else {
		liss->frames_since_draw++;
	}

	// clear clip rect before blending
	SDL_RenderSetClipRect(renderer, NULL);

	// blend in history blur_buffer for persistence effect using ADD
	SDL_SetTextureBlendMode(prv, SDL_BLENDMODE_ADD);
	SDL_SetTextureAlphaMod(prv, liss->config.persistence);
	SDL_RenderCopy(renderer, prv, NULL, NULL);

	// clip to trace color using MIN blend (prevents hue shift)
	// only if custom blend mode is supported by hardware
	if(liss->clip_texture && liss->clip_blend_mode != SDL_BLENDMODE_INVALID) {
		SDL_SetTextureBlendMode(liss->clip_texture, liss->clip_blend_mode);
		SDL_RenderCopy(renderer, liss->clip_texture, NULL, NULL);
	}

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

	// allocate buffers for 1 second of upsampled data
	liss->max_points = (uint32_t)(liss->config.upsampling * stream_config->sample_rate);

	// resampled buffer: interleaved stereo (2 channels)
	liss->resampled_buffer = malloc(liss->max_points * 2 * sizeof(float));
	CHECK_ALLOC(liss->resampled_buffer, return);

	// points buffer for drawing
	liss->points = malloc(liss->max_points * sizeof(SDL_FPoint));
	CHECK_ALLOC(liss->points, return);

	// reset continuity tracking
	liss->have_last_point = 0;
	liss->frames_since_draw = 0;

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
