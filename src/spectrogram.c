#include "spectrogram.h"
#include "s9.h"
#include "fft_analyzer.h"
#include "constant_q.h"
#include "graph_axis.h"
#include "draw_sdl.h"
#include "s9_font.h"

#include <math.h>
#include <stdlib.h>

#define THRESHOLD_DB -100.0f   // a kernel's taps below this of its peak are dropped; what that leaves out stays
                               // below -95 dB of full scale, measured by validation/scripts/measure_constant_q.py
#define READOUTS_PER_RIDGE 3.0f   // a column is read at frequencies at most this fraction of its ridge's width
                                  // apart, so a tone anywhere in it reads within 0.8 dB, measured by the same
#define FRAME_MIN 1024         // the frame's length, at least
#define FRAME_MAX 524288       // and at most: the resolution holds to f_min within it, and the parameters' ranges keep it so
                               // at 48 kHz
#define GUARD_ROWS 2           // rows kept below the plot's edge, so a late audio buffer never leaves it blank
#define EXTRA_ROWS 4           // rows the image has beyond the plot's height: the guard and the overhang above
#define CLOCK_TC 0.2           // seconds, the time constant of the audio clock's smoothing
#define CLOCK_SNAP_ROWS 4      // the clock re-seats when the arrivals get this far from its estimate: after a stall
#define GRAPH_MARGIN 10        // between the plot with its labels and the viewport
#define LABEL_MARGIN 8         // between the labels and the plot
#define FONT_SIZE 16
#define MAJOR_MIN_PX 60        // seconds are marked no closer than this, with labels
#define MINOR_MIN_PX 6         // and ticked no closer than this between them

struct spectrogram {
	spectrogram_config_t config;
	int config_changed;              // applied at the next frame
	int recolor;                     // the scale or the range changed: the image is colored again
	struct { palette_t palette; float max_db, range_db; } colored;   // what the image's colors were made with

	fft_analyzer_t *analyzer;
	s9_audio_stream_config_t stream;
	int streaming;
	uint32_t hop;                    // frames per row
	uint32_t frame;                  // the frame's length, as the range and the resolution set it
	uint64_t consumed;               // frames made into rows since streaming was prepared
	float f_hi;                      // the top of the range, within the stream's Nyquist
	char label[64];                  // the channel's name, shown in the plot's corner

	// the audio clock: the wall clock's ramp at the stream's rate plus the
	// smoothed difference between what has arrived and that ramp
	int clock_started;
	uint64_t clock_t0, clock_t;      // performance counter at the start, and at the last reading
	double clock_deviation;          // frames

	SDL_Renderer *renderer;
	SDL_Rect viewport;
	SDL_Rect plot;                   // where the image shows, within the viewport
	int image_h;                     // rows in the image: the plot's height and EXTRA_ROWS
	SDL_Texture *image;              // plot.w x image_h, a ring of rows
	SDL_Texture *unrolled;           // the ring in order, newest row last
	SDL_Texture *axes;               // the ticks, labels and frame, drawn once per viewport and stream
	int axes_drawn;
	int head;                        // the row the next hop is written to
	int rows;                        // rows the ring holds, up to image_h
	uint32_t written;                // rows written since the image started over

	constant_q_t *cq;                // one column per pixel of the plot
	constant_q_config_t built;       // what the kernels were built for
	float built_f_min, built_f_hi;
	double *edges;                   // plot.w + 1, the columns' bands
	float *history;                  // plot.w x image_h, dB: the rows as the ring holds them
	uint32_t *pixels;                // one row
	uint32_t palette[PALETTE_STEPS];
};

// -- configuration

const spectrogram_config_t *spectrogram_config(const spectrogram_t *sg) {
	return &sg->config;
}

void spectrogram_set_config(spectrogram_t *sg, const spectrogram_config_t *config) {
	const spectrogram_config_t *d = &default_spectrogram_config;
	sg->config = config ? *config : *d;
	if (sg->config.hop_ms < 1) sg->config.hop_ms = d->hop_ms;
	if (sg->config.min_window_hops < 1) sg->config.min_window_hops = 1;
	if (!(sg->config.resolution_semitones > 0.0f)) sg->config.resolution_semitones = d->resolution_semitones;
	if ((unsigned)sg->config.window_function >= WINDOW_FUNCTION_COUNT) sg->config.window_function = d->window_function;
	if (!(sg->config.range_db > 0.0f)) sg->config.range_db = d->range_db;
	if ((unsigned)sg->config.palette >= PALETTE_COUNT) sg->config.palette = d->palette;
	if (!(sg->config.f_min > 0.0f && sg->config.f_max > sg->config.f_min)) { sg->config.f_min = d->f_min; sg->config.f_max = d->f_max; }
	sg->config.grid = sg->config.grid != 0;
	sg->config_changed = 1;
}

// the frames in a row
static uint32_t hop_frames(const spectrogram_t *sg) {
	long hop = lround((double)sg->config.hop_ms * sg->stream.sample_rate / 1000.0);
	return hop < 1 ? 1 : (uint32_t)hop;
}

// The frame: the shortest fast length that holds the window of a ridge at
// f_min, the longest any column needs, within FRAME_MIN and FRAME_MAX.
static uint32_t frame_length(const spectrogram_t *sg) {
	double semitones = pow(2.0, (double)sg->config.resolution_semitones / 12.0) - 1.0;
	double need = window_function_bandwidth(sg->config.window_function) * sg->stream.sample_rate / ((double)sg->config.f_min * semitones);
	double shortest = (double)hop_frames(sg) * sg->config.min_window_hops;
	if (need < shortest) need = shortest;
	if (need < FRAME_MIN) need = FRAME_MIN;
	if (need > FRAME_MAX) need = FRAME_MAX;
	return fft_analyzer_fast_length((uint32_t)ceil(need));
}

// the frequency from which the shortest window rules: where the resolution's window would be shorter
static float floor_frequency(const spectrogram_t *sg) {
	double semitones = pow(2.0, (double)sg->config.resolution_semitones / 12.0) - 1.0;
	double shortest = (double)hop_frames(sg) * sg->config.min_window_hops;
	return (float)(window_function_bandwidth(sg->config.window_function) * sg->stream.sample_rate / (shortest * semitones));
}

// the analyzer keeps the frame and transforms nothing: the readout transforms it as it needs
static fft_analyzer_config_t analyzer_config(const spectrogram_t *sg) {
	return (fft_analyzer_config_t){
		.fft_len = sg->frame,
		.window_function = WINDOW_FUNCTION_RECTANGULAR,
		.window_skew = 0.0f,
		.mode = FFT_ANALYZER_MIX,
		.channel = sg->config.channel,
		.averaging_s = 0.0f,
		.hop_frames = sg->hop,
		.frame_only = 1,
	};
}

// hand a changed configuration to the analyzer and the coloring; the readout follows at the next row
static void apply_config(spectrogram_t *sg) {
	if (!sg->config_changed) return;
	sg->config_changed = 0;
	if (sg->config.palette != sg->colored.palette || sg->config.max_db != sg->colored.max_db || sg->config.range_db != sg->colored.range_db)
		sg->recolor = 1;
	if (!sg->stream.sample_rate) return;
	sg->hop = hop_frames(sg);
	sg->frame = frame_length(sg);
	fft_analyzer_config_t ac = analyzer_config(sg);
	fft_analyzer_set_config(sg->analyzer, &ac);
	sg->f_hi = fminf(sg->config.f_max, 0.5f * (float)sg->stream.sample_rate);
	s9_channel_label(sg->config.channel, sg->stream.channels, sg->label, sizeof sg->label);
	sg->axes_drawn = 0;
}

// the chosen scale as pixels
static void build_palette(spectrogram_t *sg) {
	const uint8_t (*table)[3] = palette_tables[sg->config.palette];
	for (int i = 0; i < PALETTE_STEPS; i++)
		sg->palette[i] = 0xFF000000u | (uint32_t)table[i][0] << 16 | (uint32_t)table[i][1] << 8 | table[i][2];
	sg->colored.palette = sg->config.palette;
	sg->colored.max_db = sg->config.max_db;
	sg->colored.range_db = sg->config.range_db;
}

// a row's levels as pixels: the range between the floor and full brightness, through the scale
static void color_row(const spectrogram_t *sg, const float *levels, uint32_t *pixels) {
	float lo = sg->config.max_db - sg->config.range_db, scale = (float)(PALETTE_STEPS - 1) / sg->config.range_db;
	for (int c = 0; c < sg->plot.w; c++) {
		float u = (levels[c] - lo) * scale;
		int i = u <= 0.0f ? 0 : u >= (float)(PALETTE_STEPS - 1) ? PALETTE_STEPS - 1 : (int)(u + 0.5f);
		pixels[c] = sg->palette[i];
	}
}

// -- the image

static void clear_image(spectrogram_t *sg) {
	sg->head = sg->rows = 0;
	sg->written = 0;
	if (!sg->image) return;
	void *pixels;
	int pitch;
	if (SDL_LockTexture(sg->image, NULL, &pixels, &pitch) == 0) {
		memset(pixels, 0, (size_t)pitch * (size_t)sg->image_h);
		SDL_UnlockTexture(sg->image);
	}
}

// the kernels for the current transform, plot and configuration; the image starts over when they change
static int ensure_kernels(spectrogram_t *sg) {
	uint32_t n = fft_analyzer_fft_len(sg->analyzer);
	uint32_t min_window = sg->hop * sg->config.min_window_hops;
	constant_q_config_t want = {
		.sample_rate = sg->stream.sample_rate,
		.n = n,
		.min_window = min_window < n ? min_window : n,
		.resolution_semitones = sg->config.resolution_semitones,
		.window_function = sg->config.window_function,
		.threshold_db = THRESHOLD_DB,
		.readouts_per_ridge = READOUTS_PER_RIDGE,
	};
	const constant_q_config_t *b = &sg->built;
	if (sg->cq && want.sample_rate == b->sample_rate && want.n == b->n && want.min_window == b->min_window &&
	    want.resolution_semitones == b->resolution_semitones && want.window_function == b->window_function &&
	    sg->built_f_min == sg->config.f_min && sg->built_f_hi == sg->f_hi)
		return 0;

	constant_q_free(&sg->cq);
	uint32_t columns = (uint32_t)sg->plot.w;
	double lo = log((double)sg->config.f_min), hi = log((double)sg->f_hi);
	for (uint32_t c = 0; c <= columns; c++)
		sg->edges[c] = exp(lo + (hi - lo) * (double)c / (double)columns);
	sg->cq = constant_q_new(&want, sg->edges, columns);
	CHECK_ALLOC(sg->cq, return -1);
	sg->built = want;
	sg->built_f_min = sg->config.f_min;
	sg->built_f_hi = sg->f_hi;
	S9_LOG_DEBUG("spectrogram: %u columns, %u taps", columns, constant_q_taps(sg->cq));
	clear_image(sg);
	return 0;
}

// one hop's readout as a row of the ring, its levels kept for coloring again
static void write_row(spectrogram_t *sg) {
	float *levels = sg->history + (size_t)sg->head * (size_t)sg->plot.w;
	constant_q_readout(sg->cq, fft_analyzer_frame(sg->analyzer, 0), levels);
	color_row(sg, levels, sg->pixels);
	SDL_UpdateTexture(sg->image, &(SDL_Rect){ 0, sg->head, sg->plot.w, 1 }, sg->pixels, sg->plot.w * (int)sizeof(uint32_t));
	sg->head = (sg->head + 1) % sg->image_h;
	if (sg->rows < sg->image_h) sg->rows++;
	sg->written++;
}

// every row of the image colored again from its levels, with the scale and range as they are now
static void recolor(spectrogram_t *sg) {
	build_palette(sg);
	if (!sg->image) return;
	void *pixels;
	int pitch;
	if (SDL_LockTexture(sg->image, NULL, &pixels, &pitch) != 0) return;
	for (int r = 0; r < sg->image_h; r++) {
		uint32_t *row = (uint32_t *)((uint8_t *)pixels + (size_t)r * (size_t)pitch);
		if (r < sg->rows) color_row(sg, sg->history + (size_t)r * (size_t)sg->plot.w, row);
		else memset(row, 0, sizeof(uint32_t) * (size_t)sg->plot.w);
	}
	SDL_UnlockTexture(sg->image);
}

// Every hop queued becomes a row, as many as a frame finds. Nothing is
// written until the transform has filled with audio once.
static void advance(spectrogram_t *sg) {
	while (fft_analyzer_queued(sg->analyzer) >= sg->hop) {
		uint32_t consumed = fft_analyzer_update(sg->analyzer);
		if (consumed < sg->hop) break;
		sg->consumed += consumed;
		if (ensure_kernels(sg) != 0) break;
		if (fft_analyzer_filled(sg->analyzer)) write_row(sg);
	}
}

// The audio's position now, in frames since streaming was prepared. What
// has arrived comes in buffers, so it is not read directly: the wall
// clock's ramp at the stream's rate carries the motion, and only the
// difference between the arrivals and the ramp is followed, smoothed,
// which also absorbs the drift between the two clocks. A difference too
// large to be jitter, after the audio has stalled or run ahead, re-seats
// the estimate instead of being followed slowly.
static double clock_now(spectrogram_t *sg) {
	uint64_t t = SDL_GetPerformanceCounter();
	double per_second = (double)SDL_GetPerformanceFrequency();
	double arrived = (double)(sg->consumed + fft_analyzer_queued(sg->analyzer));
	if (!sg->clock_started) {
		sg->clock_started = 1;
		sg->clock_t0 = sg->clock_t = t;
		sg->clock_deviation = arrived;
		return arrived;
	}
	double expected = (double)(t - sg->clock_t0) / per_second * sg->stream.sample_rate;
	double dt = (double)(t - sg->clock_t) / per_second;
	sg->clock_t = t;
	double error = arrived - expected - sg->clock_deviation;
	if (fabs(error) > (double)CLOCK_SNAP_ROWS * sg->hop) {
		sg->clock_deviation = arrived - expected;
	} else {
		sg->clock_deviation += error * (1.0 - exp(-dt / CLOCK_TC));
	}
	return expected + sg->clock_deviation;
}

// -- the plot and its axes

static void free_textures(spectrogram_t *sg) {
	if (sg->image) { SDL_DestroyTexture(sg->image); sg->image = NULL; }
	if (sg->unrolled) { SDL_DestroyTexture(sg->unrolled); sg->unrolled = NULL; }
	if (sg->axes) { SDL_DestroyTexture(sg->axes); sg->axes = NULL; }
	free(sg->edges); sg->edges = NULL;
	free(sg->history); sg->history = NULL;
	free(sg->pixels); sg->pixels = NULL;
}

static graph_hz_axis_t hz_axis(const spectrogram_t *sg) {
	return (graph_hz_axis_t){
		.min_hz = sg->config.f_min,
		.max_hz = sg->f_hi,
		.major_color = sg->config.major_color,
		.minor_color = sg->config.minor_color,
	};
}

// room for the time labels on the left, the frequency labels below
static void compute_plot(spectrogram_t *sg, TTF_Font *font) {
	int time_w = 0, time_h = 0, hz_h = 0, hz_right = 0;
	TTF_SizeUTF8(font, "100 s", &time_w, &time_h);
	graph_hz_axis_t axis = hz_axis(sg);
	graph_hz_axis_measure(&axis, font, &hz_h, &hz_right);
	int left = GRAPH_MARGIN + time_w + LABEL_MARGIN;
	int bottom = GRAPH_MARGIN + LABEL_MARGIN + hz_h;
	int top = GRAPH_MARGIN + time_h / 2;
	int right = GRAPH_MARGIN + hz_right;
	sg->plot = (SDL_Rect){ left, top, sg->viewport.w - left - right, sg->viewport.h - top - bottom };
}

// Time up the left, in the dB axis's manner: seconds marked by lines
// across the plot with their labels, no closer than MAJOR_MIN_PX, at the
// first of 1, 2, 5, 10, 20 or 50 seconds that keeps them so, and ticked in
// fifths or halves between where those stay MINOR_MIN_PX apart. The lines
// and ticks belong to the grid; the labels are drawn without it too.
static void draw_time_axis(const spectrogram_t *sg, SDL_Renderer *renderer, TTF_Font *font) {
	static const double majors[] = { 1.0, 2.0, 5.0, 10.0, 20.0, 50.0 };
	double rows_per_second = (double)sg->stream.sample_rate / (double)sg->hop;
	double major = majors[sizeof majors / sizeof *majors - 1];
	for (unsigned i = 0; i < sizeof majors / sizeof *majors; i++) {
		if (majors[i] * rows_per_second >= MAJOR_MIN_PX) { major = majors[i]; break; }
	}
	int per_major = major / 5.0 * rows_per_second >= MINOR_MIN_PX ? 5 : major / 2.0 * rows_per_second >= MINOR_MIN_PX ? 2 : 1;
	double minor = major / per_major;

	const spectrogram_config_t *c = &sg->config;
	int below = sg->plot.y + sg->plot.h;
	for (int i = 1; ; i++) {
		int y = below - 1 - (int)lround(i * minor * rows_per_second);
		if (y < sg->plot.y) break;
		SDL_SET_RENDER_DRAW_COLOR(renderer, &c->minor_color);
		if (i % per_major == 0) {
			if (c->grid) SDL_RenderDrawLine(renderer, sg->plot.x, y, sg->plot.x + sg->plot.w, y);
			char label[32];
			snprintf(label, sizeof label, "%g s", i * minor);
			draw_sdl_render_text_at_position(renderer, font, label, ANCHOR_MIDDLE_RIGHT, c->major_color, sg->plot.x - LABEL_MARGIN, y);
		} else if (c->grid) {
			SDL_RenderDrawLine(renderer, sg->plot.x, y, sg->plot.x + GRAPH_DB_AXIS_TICK_LENGTH, y);
		}
	}
}

// The graticule as the spectrum's graph draws it: frequency across the
// bottom, time up the left, a frame, and the channel's name in the
// plot's corner. Without the grid, the labels and the frame alone.
static void draw_axes(spectrogram_t *sg, SDL_Renderer *renderer) {
	if (sg->axes) SDL_DestroyTexture(sg->axes);
	sg->axes = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, sg->viewport.w, sg->viewport.h);
	CHECK_ALLOC(sg->axes, return);
	SDL_SetTextureBlendMode(sg->axes, SDL_BLENDMODE_BLEND);
	CHECK_ERROR(SDL_SetRenderTarget(renderer, sg->axes) == 0, SDL_DestroyTexture(sg->axes); sg->axes = NULL; return, "could not set render target");
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);

	const spectrogram_config_t *c = &sg->config;
	TTF_Font *font = s9_open_font(FONT_SIZE);
	if (font) {
		graph_hz_axis_t axis = hz_axis(sg);
		if (c->grid) graph_hz_axis_draw(&axis, renderer, font, &sg->plot, LABEL_MARGIN);
		else graph_hz_axis_draw_labels(&axis, renderer, font, &sg->plot, LABEL_MARGIN);
		draw_time_axis(sg, renderer, font);
		if (sg->label[0]) {
			draw_sdl_render_text_at_position(renderer, font, sg->label, ANCHOR_TOP_LEFT, c->major_color,
			                                 sg->plot.x + GRAPH_DB_AXIS_TICK_LENGTH + 4, sg->plot.y + 4);
		}
		TTF_CloseFont(font);
	}

	sdl_polygon_t *frame = frame_new_from_rect(sg->plot, 2.0, c->major_color);
	if (frame) {
		SDL_RENDER_GEOMETRY_POLYGON(renderer, frame);
		sdl_polygon_free(&frame);
	}
	SDL_SetRenderTarget(renderer, NULL);
	sg->axes_drawn = 1;
}

// the textures and buffers for the viewport; the image starts over with a new one
static int prepare_to_draw(spectrogram_t *sg, SDL_Renderer *renderer) {
	SDL_Rect viewport;
	SDL_RenderGetViewport(renderer, &viewport);
	if (sg->renderer == renderer && SDL_RectEquals(&sg->viewport, &viewport) && sg->image) {
		if (!sg->axes_drawn) draw_axes(sg, renderer);
		return 0;
	}
	sg->renderer = renderer;
	sg->viewport = viewport;
	free_textures(sg);
	constant_q_free(&sg->cq);

	TTF_Font *font = s9_open_font(FONT_SIZE);
	CHECK_ERROR(font != NULL, return -1, "unable to open font");
	compute_plot(sg, font);
	TTF_CloseFont(font);
	if (sg->plot.w < 2 || sg->plot.h < 1) return -1;
	sg->image_h = sg->plot.h + EXTRA_ROWS;

	sg->image = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, sg->plot.w, sg->image_h);
	CHECK_ALLOC(sg->image, return -1);
	SDL_SetTextureBlendMode(sg->image, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(sg->image, SDL_ScaleModeNearest);
	sg->unrolled = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, sg->plot.w, sg->image_h);
	CHECK_ALLOC(sg->unrolled, free_textures(sg); return -1);
	SDL_SetTextureBlendMode(sg->unrolled, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(sg->unrolled, SDL_ScaleModeLinear);   // a fractional offset blends neighbouring rows
	sg->edges = malloc(sizeof(double) * ((size_t)sg->plot.w + 1));
	CHECK_ALLOC(sg->edges, free_textures(sg); return -1);
	sg->history = malloc(sizeof(float) * (size_t)sg->plot.w * (size_t)sg->image_h);
	CHECK_ALLOC(sg->history, free_textures(sg); return -1);
	sg->pixels = malloc(sizeof(uint32_t) * sg->plot.w);
	CHECK_ALLOC(sg->pixels, free_textures(sg); return -1);
	clear_image(sg);
	draw_axes(sg, renderer);
	return 0;
}

// The ring unrolled, then placed so that the plot's bottom edge is the
// audio clock's now less the guard: the newest row sits below the edge by
// what has arrived since it, moving up as the clock runs and dropping
// back a row as each row is written, so the picture never jumps.
static void draw(spectrogram_t *sg, SDL_Renderer *renderer) {
	int w = sg->plot.w, h = sg->image_h;
	SDL_SetRenderTarget(renderer, sg->unrolled);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	if (sg->rows == h && sg->head < h)
		SDL_RenderCopy(renderer, sg->image, &(SDL_Rect){ 0, sg->head, w, h - sg->head }, &(SDL_Rect){ 0, 0, w, h - sg->head });
	if (sg->head > 0)
		SDL_RenderCopy(renderer, sg->image, &(SDL_Rect){ 0, 0, w, sg->head }, &(SDL_Rect){ 0, h - sg->head, w, sg->head });
	SDL_SetRenderTarget(renderer, NULL);

	double below = ((double)sg->consumed + (double)GUARD_ROWS * sg->hop - clock_now(sg)) / (double)sg->hop;   // rows
	float bottom = (float)(sg->plot.y + sg->plot.h) + (float)below;
	SDL_RenderSetClipRect(renderer, &sg->plot);
	SDL_RenderCopyF(renderer, sg->unrolled, NULL, &(SDL_FRect){ (float)sg->plot.x, bottom - (float)h, (float)w, (float)h });
	SDL_RenderSetClipRect(renderer, NULL);
	if (sg->axes) SDL_RenderCopy(renderer, sg->axes, NULL, &(SDL_Rect){ 0, 0, sg->viewport.w, sg->viewport.h });
}

// -- lifecycle

spectrogram_t *spectrogram_new(const spectrogram_config_t *config) {
	spectrogram_t *sg = malloc(sizeof(spectrogram_t));
	CHECK_ALLOC(sg, return NULL);
	memset(sg, 0, sizeof(*sg));
	spectrogram_set_config(sg, config);
	sg->config_changed = 0;
	sg->frame = FRAME_MIN;
	sg->analyzer = fft_analyzer_new(&(fft_analyzer_config_t){
		.fft_len = FRAME_MIN, .window_function = WINDOW_FUNCTION_RECTANGULAR, .mode = FFT_ANALYZER_MIX, .channel = sg->config.channel,
	});
	CHECK_ALLOC(sg->analyzer, free(sg); return NULL);
	build_palette(sg);
	return sg;
}

void spectrogram_free(spectrogram_t **sg) {
	if (sg && *sg) {
		free_textures(*sg);
		constant_q_free(&(*sg)->cq);
		fft_analyzer_free(&(*sg)->analyzer);
		free(*sg);
		*sg = NULL;
	}
}

void spectrogram_render(spectrogram_t *sg, SDL_Renderer *renderer) {
	if (!sg->streaming) return;
	apply_config(sg);
	if (prepare_to_draw(sg, renderer) != 0) return;
	if (sg->recolor) {
		recolor(sg);
		sg->recolor = 0;
	}
	advance(sg);
	draw(sg, renderer);
}

uint32_t spectrogram_rows(const spectrogram_t *sg) {
	return sg->written;
}

// what the configuration as set gives, whether or not a frame has applied it yet
uint32_t spectrogram_frame(const spectrogram_t *sg) {
	return sg->stream.sample_rate ? frame_length(sg) : 0;
}

float spectrogram_time_resolution(const spectrogram_t *sg) {
	return sg->stream.sample_rate ? (float)frame_length(sg) / (float)sg->stream.sample_rate : 0.0f;
}

float spectrogram_floor_frequency(const spectrogram_t *sg) {
	return sg->stream.sample_rate ? floor_frequency(sg) : 0.0f;
}

void spectrogram_stop_streaming(spectrogram_t *sg) {
	fft_analyzer_stop_streaming(sg->analyzer);
	sg->streaming = 0;
}

void spectrogram_prepare_to_stream(spectrogram_t *sg, const s9_audio_stream_config_t *stream_config) {
	CHECK_PARAMETER(stream_config->samples_per_display_frame > 0 && stream_config->sample_rate > 0, return);
	sg->stream = *stream_config;
	sg->consumed = 0;
	sg->clock_started = 0;
	sg->config_changed = 1;
	apply_config(sg);
	fft_analyzer_prepare_to_stream(sg->analyzer, stream_config);
	sg->streaming = fft_analyzer_streaming(sg->analyzer);
	clear_image(sg);
}

void spectrogram_enqueue_audio_data(spectrogram_t *sg, const void *data, uint32_t size) {
	fft_analyzer_enqueue_audio_data(sg->analyzer, data, size);
}
