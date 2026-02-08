#include "oscilloscope.h"

#include "s9.h"
#include "osc_sampler.h"
#include "draw_sdl.h"
#include "graticule.h"

#include <math.h>

#define FAST_MODE_SWEEPS_PER_SEC	10.0f   // sweeps faster than this rate use fast mode
#define FAST_MODE_HOLD_TIME			0.0667f // seconds to keep displaying last sweep while waiting for next

typedef struct {
	float time_per_display;	// seconds
	float amp_per_display;  // <= 2.0
} oscilloscope_settings_t;

static const oscilloscope_settings_t default_settings = {
	.time_per_display = 0.01,
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
	SDL_Texture *clip_texture;             // filled with trace color for MIN clipping
	SDL_BlendMode clip_blend_mode;         // MIN blend mode

	oscilloscope_settings_t settings;
	sdl_path_t *path;    // last drawn sweep path (persists across render calls)

	enum { osc_mode_none, osc_mode_fast, osc_mode_slow } render_mode;
	union {
		struct {
			uint32_t fill;       // pixels accumulated in pixel_buffer
			uint32_t path_hold;  // frames remaining to redraw saved path
		} fast;
		struct {
			uint32_t sweep_pos;        // current screen x position in sweep
			float last_value;          // last pixel value drawn (for path continuity)
			uint32_t frames_since_draw; // frames elapsed since last segment was drawn
		} slow;
	};

} oscilloscope_t;

static void free_dsp_allocations(oscilloscope_t *osc) {

	// clear flag and stream config
	osc->streaming = 0;
	osc->stream_config.audio_format.sample_type = S9_SAMPLE_NONE;

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

	if(osc->clip_texture) {
		SDL_DestroyTexture(osc->clip_texture);
		osc->clip_texture = NULL;
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
	oscilloscope_t *osc = (oscilloscope_t *) malloc(sizeof(oscilloscope_t));
	CHECK_ALLOC(osc, return NULL);
	memset(osc, 0, sizeof(*osc));
	oscilloscope_set_config(osc, config);

	osc->settings = default_settings;

	// create graticule with default config
	osc->graticule = graticule_new(NULL);
	CHECK_ALLOC(osc->graticule, free(osc); return NULL);

	// create MIN blend mode for clipping to trace color
	// runtime detection will disable this for unsupported renderers
	osc->clip_blend_mode = SDL_ComposeCustomBlendMode(
		SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MINIMUM,
		SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MINIMUM
	);

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

static int prepare_to_draw(oscilloscope_t *osc, SDL_Renderer *renderer) {
	SDL_Rect viewport;
	SDL_RenderGetViewport(renderer, &viewport);

	if(osc->renderer != renderer || !SDL_RectEquals(&osc->viewport, &viewport)) {
		osc->renderer = renderer;
		osc->viewport = viewport;

		// check if renderer supports MIN blend operation
		// Only Direct3D 9/11 and Metal are known to fully support it
		// OpenGL ES doesn't support it at all
		// Desktop OpenGL support varies by driver (Pi reports "opengl" but uses ES)
		SDL_RendererInfo info;
		int min_blend_supported = 0;
		if(SDL_GetRendererInfo(renderer, &info) == 0) {
			const char *name = info.name;
			// Enable for renderers we know support MIN blend
			if(strstr(name, "direct3d") != NULL || strstr(name, "metal") != NULL) {
				min_blend_supported = 1;
			}
		}
		if(!min_blend_supported) {
			osc->clip_blend_mode = SDL_BLENDMODE_INVALID;
		}

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

		// create clip texture filled with trace color for MIN blending
		// only if custom blend mode is supported
		if(osc->clip_blend_mode != SDL_BLENDMODE_INVALID) {
			osc->clip_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, osc->viewport.w, osc->viewport.h);
			CHECK_ALLOC(osc->clip_texture, return -1);
			SDL_SetRenderTarget(renderer, osc->clip_texture);
			SDL_SetRenderDrawColor(renderer, osc->config.trace_color.r, osc->config.trace_color.g, osc->config.trace_color.b, 255);
			SDL_RenderClear(renderer);
		}

		osc->frame_rect = (SDL_Rect){ .x = 10, .y = 10, .w = viewport.w - 20, .h = viewport.h - 20 };

		free(osc->pixel_buffer);
		osc->pixel_buffer_size = (uint32_t)osc->frame_rect.w;
		osc->pixel_buffer = (float *) malloc(osc->pixel_buffer_size * sizeof(float));
		CHECK_ALLOC(osc->pixel_buffer, return -1);

		osc->render_mode = osc_mode_none;
	}

	return 0;
}

// build path from pixel samples, save on osc (freeing previous), and render
// pixel_buf contains n_pixels sample values; x_start is the screen x offset for the first pixel
static void build_and_render_path(oscilloscope_t *osc, SDL_Renderer *renderer,
                                  const float *pixel_buf, uint32_t n_pixels, uint32_t x_start,
                                  float vertical_factor, float vertical_offset) {
	SDL_FPoint *points = (SDL_FPoint *) malloc(sizeof(SDL_FPoint) * n_pixels);
	CHECK_ALLOC(points, return);

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
// osc->fast.fill tracks the pixel_buffer fill level.
// when no new sweep is drawn, redraw the saved path for up to path_hold frames.
static void render_fast(oscilloscope_t *osc, SDL_Renderer *renderer,
                        float pixel_rate, float gain, uint32_t sweep_width, uint32_t path_hold_frames,
                        float vertical_factor, float vertical_offset) {
	int drew = 0;

	while(1) {
		uint32_t pixels = osc_sampler_read_pixels(
			osc->sampler, osc->pixel_buffer + osc->fast.fill,
			sweep_width - osc->fast.fill, pixel_rate, gain, NULL
		);

		if(pixels == 0) break;

		osc->fast.fill += pixels;

		if(osc->fast.fill >= sweep_width) {
			build_and_render_path(osc, renderer, osc->pixel_buffer,
			                      sweep_width, 0, vertical_factor, vertical_offset);
			drew = 1;
			osc->fast.path_hold = path_hold_frames;
			osc_sampler_sweep_complete(osc->sampler);
			osc->fast.fill = 0;
		}
	}

	// if no new sweep drawn, redraw saved path while hold remains
	if(!drew && osc->path && osc->fast.path_hold > 0) {
		sdl_path_render(osc->path, renderer);
		osc->fast.path_hold--;
	}
}

// render a path segment with exponential per-vertex fade to simulate continuous persistence.
// the rightmost pixel gets full trace_color; the leftmost gets trace_color * p^K,
// where K = frames_since_draw (actual persistence fades the previous boundary has been through).
// within the segment, the exponent interpolates linearly from K (left) to 0 (right).
static void render_gradient_path(oscilloscope_t *osc, SDL_Renderer *renderer,
                                 const float *pixel_buf, uint32_t n_pixels, uint32_t x_start,
                                 float vertical_factor, float vertical_offset,
                                 SDL_Color trace_color, float p, uint32_t frames_since_draw) {
	if(n_pixels < 2) return;

	uint32_t n_verts = 2 * n_pixels;
	uint32_t n_indices = (n_pixels - 1) * 6;
	float half_width = 0.5f * osc->config.trace_width;

	SDL_Vertex *vertices = (SDL_Vertex *) malloc(n_verts * sizeof(SDL_Vertex));
	CHECK_ALLOC(vertices, return);

	int *indices = (int *) malloc(n_indices * sizeof(int));
	CHECK_ALLOC(indices, free(vertices); return);

	// left edge fades by p^K; right edge is full brightness.
	// iterate from left to right, multiplying by a constant step each pixel.
	float K = (float)frames_since_draw;
	float factor = powf(p, K);
	float step = (factor > 0.0f) ? powf(factor, -1.0f / (float)(n_pixels - 1)) : 1.0f;

	for(uint32_t i = 0; i < n_pixels; i++) {
		float cx = osc->frame_rect.x + x_start + i;
		float cy = vertical_offset + pixel_buf[i] * vertical_factor;

		SDL_Color c = {
			(uint8_t)(trace_color.r * factor),
			(uint8_t)(trace_color.g * factor),
			(uint8_t)(trace_color.b * factor),
			255
		};

		// perpendicular vector (same convention as sdl_path_new)
		float dx, dy;
		if(i == 0) {
			dx = pixel_buf[1] - pixel_buf[0];
			dy = 1.0f;
		} else if(i == n_pixels - 1) {
			dx = pixel_buf[n_pixels - 1] - pixel_buf[n_pixels - 2];
			dy = 1.0f;
		} else {
			dx = pixel_buf[i + 1] - pixel_buf[i - 1];
			dy = 2.0f;
		}
		dx *= vertical_factor;
		float l_r = half_width / sqrtf(dx * dx + dy * dy);
		dx *= l_r;
		dy *= l_r;

		// inner edge (index i)
		vertices[i] = (SDL_Vertex){
			.position = { cx + dx, cy - dy },
			.color = c,
			.tex_coord = { 0, 0 }
		};

		// outer edge (reversed, matching sdl_path_new layout)
		vertices[n_verts - 1 - i] = (SDL_Vertex){
			.position = { cx - dx, cy + dy },
			.color = c,
			.tex_coord = { 0, 0 }
		};

		factor *= step;
	}

	// triangle strip indices (same layout as sdl_path_new)
	for(uint32_t i = 0; i < n_pixels - 1; i++) {
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

// slow mode: draw pixel data incrementally as it arrives.
// osc->slow.sweep_pos tracks the current screen x position within the sweep.
// pixel_buffer is used as scratch space (always written from index 0).
// each segment is drawn with an exponential color gradient based on actual
// frames elapsed since the last draw, so boundary brightness always matches
// regardless of data arrival timing.
static void render_slow(oscilloscope_t *osc, SDL_Renderer *renderer,
                        float pixel_rate, float gain, uint32_t sweep_width,
                        float vertical_factor, float vertical_offset) {
	float p = osc->config.persistence / 255.0f;
	int drew = 0;

	while(1) {
		// when continuing a sweep, reserve pixel_buffer[0] for the join point
		int joining = (osc->slow.sweep_pos > 0);
		float *dest = osc->pixel_buffer + joining;

		uint32_t pixels = osc_sampler_read_pixels(
			osc->sampler, dest,
			sweep_width - osc->slow.sweep_pos, pixel_rate, gain, NULL
		);
		if(pixels == 0) break;

		// prepend previous endpoint to form a continuous path
		if(joining) osc->pixel_buffer[0] = osc->slow.last_value;

		osc->slow.last_value = dest[pixels - 1];

		uint32_t path_len = pixels + joining;
		if(path_len >= 2) {
			// first segment this frame: use actual frames elapsed (+1 for this frame's blend).
			// subsequent segments in the same frame: K=0 (no persistence fade between them).
			uint32_t K = drew ? 0 : osc->slow.frames_since_draw + 1;
			render_gradient_path(osc, renderer, osc->pixel_buffer, path_len,
			                     osc->slow.sweep_pos - joining, vertical_factor, vertical_offset,
			                     osc->config.trace_color, p, K);
		}

		drew = 1;
		osc->slow.sweep_pos += pixels;

		if(osc->slow.sweep_pos >= sweep_width) {
			osc_sampler_sweep_complete(osc->sampler);
			osc->slow.sweep_pos = 0;
		}
	}

	if(drew) {
		osc->slow.frames_since_draw = 0;
	} else {
		osc->slow.frames_since_draw++;
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
		float fast_mode_min_time = 1.0f / FAST_MODE_SWEEPS_PER_SEC;
		float frame_period = (float)osc->stream_config.samples_per_display_frame / osc->stream_config.sample_rate;
		float fast_mode_threshold = ceilf(fast_mode_min_time / frame_period) * frame_period;
		uint32_t path_hold_frames = (uint32_t)ceilf(FAST_MODE_HOLD_TIME / frame_period);

		typeof(osc->render_mode) mode = (osc->settings.time_per_display < fast_mode_threshold)
			? osc_mode_fast : osc_mode_slow;

		if(osc->render_mode != mode) {
			osc->render_mode = mode;
			memset(&osc->fast, 0, sizeof(osc->fast));  // zero the union
		}

		if(mode == osc_mode_fast) {
			render_fast(osc, renderer, pixel_rate, gain, sweep_width, path_hold_frames,
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

	// clip to trace color using MIN blend (prevents hue shift)
	// only if custom blend mode is supported by hardware
	if(osc->clip_texture && osc->clip_blend_mode != SDL_BLENDMODE_INVALID) {
		SDL_SetTextureBlendMode(osc->clip_texture, osc->clip_blend_mode);
		SDL_RenderCopy(renderer, osc->clip_texture, NULL, NULL);
	}

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

	// create sampler with capacity for 1 second of data
	uint32_t buffer_size = stream_config->sample_rate;
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
