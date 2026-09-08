#include "harmonics.h"
#include "s9.h"
#include "s9_font.h"
#include "draw_sdl.h"
#include "graph_axis.h"
#include "fft_analyzer.h"
#include "pitch.h"
#include "harmonic_readout.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define LABEL_MARGIN 8          // between labels and the plot
#define GRAPH_MARGIN 10         // between the graph and the viewport
#define FONT_SIZE 16
#define MAX_HARMONICS 64
#define PEAK_TOLERANCE 0.02f    // how far below the highest autocorrelation peak an earlier one may be and still be the period
#define SEARCH_MIN_BINS 3       // a readout looks at least this far either side of its harmonic
#define SEARCH_RATIO 0.01f      // and at least this fraction of the harmonic's frequency, covering the reported fundamental's dead band and smoothing lag
#define SHOULDER_BINS 32        // the floor beside a harmonic is read from this many bins per side, fewer when the next harmonic is closer
#define PROMINENCE_DB 15.0f     // a harmonic must stand this far above the floor beside it
#define AXIS_HEADROOM 0.5f      // the axis extends this many fundamentals past the last marker
#define PLOT_WIDTH 2.0f
#define LABEL_TEXT 32

// text drawn from a texture that is remade only when the text changes
typedef struct {
	SDL_Texture *texture;
	int w, h;
	char text[LABEL_TEXT];
} label_t;

struct harmonics {
	harmonics_config_t config;
	int config_changed;              // applied at the next frame

	fft_analyzer_t *analyzer;
	pitch_conditioner_t *conditioner;
	uint32_t buffer_len;             // the transform length nacf and db are sized for
	float *nacf;
	float *db;                       // the latest spectrum, per bin
	harmonic_window_t window;        // the readouts' model of the analyzer's window
	uint32_t window_len;             // the transform length it was computed for; 0 before any

	bool pitch_valid;
	float pitch_frequency;

	SDL_Renderer *renderer;
	SDL_Rect viewport;
	TTF_Font *font;
	SDL_Rect plot_rect;
	SDL_Texture *graticule;
	float *plot;                     // dB per plot column
	SDL_FPoint *points;
	label_t readouts[MAX_HARMONICS]; // the level read at each harmonic
	label_t numbers[MAX_HARMONICS];  // the harmonic numbers under the markers
	label_t header;                  // the fundamental
};

// -- configuration

static fft_analyzer_config_t analyzer_config(const harmonics_t *h) {
	return (fft_analyzer_config_t){
		.resolution_hz = h->config.resolution_hz,
		.window_function = h->config.window_function,
		.window_skew = 0.0f,
		.mode = FFT_ANALYZER_MIX,
		.channel = h->config.channel,
	};
}

static pitch_conditioner_config_t conditioner_config(const harmonics_t *h) {
	return (pitch_conditioner_config_t){
		.gate_rms = h->config.gate_rms,
		.clarity_on = h->config.clarity_on,
		.clarity_off = h->config.clarity_off,
		.hold_frames = h->config.hold_frames,
		.octave_tolerance_cents = h->config.octave_tolerance_cents,
		.smoothing = h->config.smoothing,
		.deadband_cents = h->config.deadband_cents,
	};
}

static graph_db_axis_t db_axis(const harmonics_t *h) {
	return (graph_db_axis_t){
		.min_db = h->config.min_db,
		.max_db = h->config.max_db,
		.major_interval = h->config.major_db_interval,
		.minor_interval = h->config.minor_db_interval,
		.major_color = h->config.major_color,
		.minor_color = h->config.minor_color,
	};
}

const harmonics_config_t *harmonics_config(const harmonics_t *h) {
	return &h->config;
}

void harmonics_set_config(harmonics_t *h, const harmonics_config_t *config) {
	h->config = config ? *config : default_harmonics_config;
	if (h->config.harmonics < 1) h->config.harmonics = 1;
	if (h->config.harmonics > MAX_HARMONICS) h->config.harmonics = MAX_HARMONICS;
	if (!(h->config.resolution_hz > 0.0f)) h->config.resolution_hz = default_harmonics_config.resolution_hz;
	h->config_changed = 1;
}

// hand a changed configuration to the analyzer and the conditioner
static void apply_config(harmonics_t *h) {
	if (!h->config_changed) return;
	h->config_changed = 0;

	fft_analyzer_config_t wanted = analyzer_config(h);
	const fft_analyzer_config_t *current = fft_analyzer_config(h->analyzer);
	if (wanted.resolution_hz != current->resolution_hz || wanted.window_function != current->window_function) {
		fft_analyzer_set_config(h->analyzer, &wanted);
		h->window_len = 0;
	} else if (wanted.channel != current->channel) {
		fft_analyzer_set_channel(h->analyzer, wanted.channel);
	}
	pitch_conditioner_config_t cc = conditioner_config(h);
	pitch_conditioner_set_config(h->conditioner, &cc);
}

// -- lifecycle

static void free_label(label_t *l) {
	if (l->texture) SDL_DestroyTexture(l->texture);
	l->texture = NULL;
	l->text[0] = '\0';
}

static void free_drawing(harmonics_t *h) {
	if (h->graticule) { SDL_DestroyTexture(h->graticule); h->graticule = NULL; }
	free(h->plot); h->plot = NULL;
	free(h->points); h->points = NULL;
	for (int i = 0; i < MAX_HARMONICS; i++) {
		free_label(&h->readouts[i]);
		free_label(&h->numbers[i]);
	}
	free_label(&h->header);
	if (h->font) { TTF_CloseFont(h->font); h->font = NULL; }
	h->renderer = NULL;
}

harmonics_t *harmonics_new(const harmonics_config_t *config) {
	harmonics_t *h = malloc(sizeof(harmonics_t));
	CHECK_ALLOC(h, return NULL);
	memset(h, 0, sizeof(*h));

	harmonics_set_config(h, config);
	h->config_changed = 0;

	fft_analyzer_config_t ac = analyzer_config(h);
	h->analyzer = fft_analyzer_new(&ac);
	CHECK_ALLOC(h->analyzer, free(h); return NULL);

	h->conditioner = malloc(pitch_conditioner_alloc_size());
	CHECK_ALLOC(h->conditioner, harmonics_free(&h); return NULL);
	pitch_conditioner_config_t cc = conditioner_config(h);
	pitch_conditioner_init(h->conditioner, &cc);
	return h;
}

void harmonics_free(harmonics_t **h) {
	if (h && *h) {
		free_drawing(*h);
		fft_analyzer_free(&(*h)->analyzer);
		free((*h)->conditioner);
		free((*h)->nacf);
		free((*h)->db);
		free(*h);
		*h = NULL;
	}
}

bool harmonics_get_pitch_valid(const harmonics_t *h) { return h->pitch_valid; }
float harmonics_get_pitch_frequency(const harmonics_t *h) { return h->pitch_frequency; }

// -- streaming

void harmonics_prepare_to_stream(harmonics_t *h, const s9_audio_stream_config_t *stream_config) {
	fft_analyzer_prepare_to_stream(h->analyzer, stream_config);
	pitch_conditioner_reset(h->conditioner);
	h->pitch_valid = false;
	h->pitch_frequency = 0.0f;
	h->window_len = 0;
}

void harmonics_stop_streaming(harmonics_t *h) {
	fft_analyzer_stop_streaming(h->analyzer);
	h->renderer = NULL;
}

void harmonics_enqueue_audio_data(harmonics_t *h, const void *data, uint32_t size) {
	fft_analyzer_enqueue_audio_data(h->analyzer, data, size);
}

// -- analysis

// the fundamental and the spectrum from the latest transform
static int analyze(harmonics_t *h) {
	fft_analyzer_t *fa = h->analyzer;
	uint32_t n = fft_analyzer_fft_len(fa), bins = n / 2 + 1;
	float fs = (float)fft_analyzer_sample_rate(fa);

	if (h->buffer_len != n) {
		free(h->nacf); free(h->db);
		h->nacf = malloc(sizeof(float) * n);
		h->db = malloc(sizeof(float) * bins);
		h->buffer_len = h->nacf && h->db ? n : 0;
		CHECK_ALLOC(h->nacf, return -1);
		CHECK_ALLOC(h->db, return -1);
	}
	if (h->window_len != n) {
		harmonic_window_compute(&h->window, fft_analyzer_window(fa), n);
		h->window_len = n;
	}

	// the period lies between the lags of f_max and f_min
	uint32_t lag_min = (uint32_t)floorf(fs / h->config.f_max);
	uint32_t lag_max = (uint32_t)ceilf(fs / h->config.f_min);
	if (lag_min < 2) lag_min = 2;
	if (lag_max > n - 2) lag_max = n - 2;
	uint32_t lags = lag_max + 2;

	pitch_estimate_t e = { 0.0f, 0.0f };
	if (fft_analyzer_nacf(fa, 0, h->nacf, lags) == 0)
		e = pitch_estimate(h->nacf, lags, lag_min, lag_max, PEAK_TOLERANCE);
	float rms = fft_analyzer_rms(fa, 0);
	h->pitch_valid = pitch_conditioner_update(h->conditioner, e.period, e.clarity, rms, fs, &h->pitch_frequency);

	harmonic_magnitude_db((const float *)fft_analyzer_spectrum(fa, 0), bins, n, h->db);
	return 0;
}

// -- drawing

static void label_set(label_t *l, SDL_Renderer *renderer, TTF_Font *font, SDL_Color color, const char *text) {
	if (strcmp(l->text, text) == 0) return;
	snprintf(l->text, sizeof(l->text), "%s", text);
	if (!text[0]) {
		free_label(l);
		return;
	}
	draw_sdl_render_text(renderer, &l->texture, font, &color, text, &l->w, &l->h);
}

static void label_blit(const label_t *l, SDL_Renderer *renderer, float x, float y, anchor_position_t anchor) {
	if (l->texture) draw_sdl_blit_texture(renderer, l->texture, x, y, anchor);
}

// the axes and the frame, into the current render target, sizing the plot
static void draw_graticule(harmonics_t *h, SDL_Renderer *renderer) {
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);

	graph_db_axis_t axis = db_axis(h);
	int label_w, label_h;
	graph_db_axis_measure(&axis, h->font, &label_w, &label_h);

	int left = label_w + LABEL_MARGIN + GRAPH_MARGIN;
	int top = label_h + LABEL_MARGIN + GRAPH_MARGIN;       // the header's line
	int bottom = label_h + LABEL_MARGIN + GRAPH_MARGIN;    // the harmonic numbers' line
	int right = GRAPH_MARGIN;
	h->plot_rect = (SDL_Rect){ left, top, h->viewport.w - left - right, h->viewport.h - top - bottom };

	graph_db_axis_draw(&axis, renderer, h->font, &h->plot_rect, LABEL_MARGIN);

	sdl_polygon_t *frame = frame_new_from_rect(h->plot_rect, 2.0, h->config.major_color);
	if (frame) {
		SDL_RENDER_GEOMETRY_POLYGON(renderer, frame);
		sdl_polygon_free(&frame);
	}
}

static void prepare_to_draw(harmonics_t *h, SDL_Renderer *renderer) {
	SDL_Rect viewport;
	SDL_RenderGetViewport(renderer, &viewport);
	if (h->renderer == renderer && SDL_RectEquals(&h->viewport, &viewport)) return;

	free_drawing(h);
	h->renderer = renderer;
	h->viewport = viewport;

	h->font = s9_open_font(FONT_SIZE);
	CHECK_ERROR(h->font != NULL, return, "unable to create font");

	h->graticule = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, viewport.w, viewport.h);
	CHECK_ALLOC(h->graticule, return);
	CHECK_ERROR(SDL_SetTextureBlendMode(h->graticule, SDL_BLENDMODE_BLEND) == 0, free_drawing(h); return, "could not set blend mode");
	CHECK_ERROR(SDL_SetRenderTarget(renderer, h->graticule) == 0, free_drawing(h); return, "could not set render target");
	draw_graticule(h, renderer);
	SDL_SetRenderTarget(renderer, NULL);

	if (h->plot_rect.w < 2 || h->plot_rect.h < 2) {
		free_drawing(h);
		return;
	}
	h->plot = malloc(sizeof(float) * h->plot_rect.w);
	h->points = malloc(sizeof(SDL_FPoint) * h->plot_rect.w);
	CHECK_ALLOC(h->plot, free_drawing(h); return);
	CHECK_ALLOC(h->points, free_drawing(h); return);
}

// the spectrum from 0 to f_end Hz across the plot columns: the loudest bin
// in each column, or the level between bins where a column holds none
static void reduce_plot(harmonics_t *h, double f_end, double delta_f, uint32_t bins) {
	int w = h->plot_rect.w;
	float floor_db = (float)h->config.min_db - 10.0f;   // off the bottom of the plot
	for (int i = 0; i < w; i++) {
		double lo = f_end * i / w, hi = f_end * (i + 1) / w;
		uint32_t b0 = (uint32_t)ceil(lo / delta_f);
		uint32_t b1 = (uint32_t)ceil(hi / delta_f);   // one past the last bin below hi
		if (b1 > bins) b1 = bins;
		float v;
		if (b0 < b1) {
			v = h->db[b0];
			for (uint32_t b = b0 + 1; b < b1; b++)
				if (h->db[b] > v) v = h->db[b];
		} else {
			double f = 0.5 * (lo + hi) / delta_f;
			uint32_t b = (uint32_t)f;
			if (b + 1 >= bins) {
				v = h->db[bins - 1];
			} else {
				float t = (float)(f - b);
				v = isfinite(h->db[b]) && isfinite(h->db[b + 1]) ? h->db[b] + t * (h->db[b + 1] - h->db[b]) : floor_db;
			}
		}
		if (!(v > floor_db)) v = floor_db;
		h->plot[i] = v;
	}
}

static void draw(harmonics_t *h, SDL_Renderer *renderer) {
	fft_analyzer_t *fa = h->analyzer;
	uint32_t n = fft_analyzer_fft_len(fa), bins = n / 2 + 1;
	double fs = fft_analyzer_sample_rate(fa);
	double delta_f = fs / n;
	double f0 = h->pitch_frequency;
	uint32_t count = h->config.harmonics;

	// the axis: the harmonics across, or the whole band before a fundamental is known
	double f_end = f0 > 0.0 ? (count + AXIS_HEADROOM) * f0 : fs / 2.0;
	if (f_end > fs / 2.0) f_end = fs / 2.0;

	SDL_Rect save_viewport;
	SDL_RenderGetViewport(renderer, &save_viewport);
	const SDL_Rect *plot = &h->plot_rect;

	// the spectrum, clipped to the plot
	reduce_plot(h, f_end, delta_f, bins);
	float db_scale = (float)plot->h / (float)(h->config.max_db - h->config.min_db);
	for (int i = 0; i < plot->w; i++)
		h->points[i] = (SDL_FPoint){ (float)i, ((float)h->config.max_db - h->plot[i]) * db_scale };
	SDL_Rect plot_viewport = { save_viewport.x + plot->x, save_viewport.y + plot->y, plot->w, plot->h };
	SDL_RenderSetViewport(renderer, &plot_viewport);
	sdl_path_t *path = sdl_path_new(&h->config.plot_color, PLOT_WIDTH, 1, h->points, plot->w);
	if (path) {
		sdl_path_render(path, renderer);
		sdl_path_free(&path);
	}
	SDL_RenderSetViewport(renderer, &save_viewport);

	SDL_Rect dst = { 0, 0, h->viewport.w, h->viewport.h };
	SDL_RenderCopy(renderer, h->graticule, NULL, &dst);

	// markers, numbers, and readouts at the harmonics
	if (f0 > 0.0) {
		SDL_Color marker = h->pitch_valid ? h->config.marker_color : h->config.held_color;

		// the floor beside a harmonic is read short of the next harmonic's lobe
		long fit = (long)(f0 / delta_f / 2.0) - (long)h->window.reach;
		uint32_t shoulder = fit < 1 ? 1 : fit < SHOULDER_BINS ? (uint32_t)fit : SHOULDER_BINS;

		for (uint32_t k = 1; k <= count; k++) {
			double fk = k * f0;
			if (fk > f_end) break;
			float x = plot->x + (float)(plot->w * fk / f_end);

			SDL_SET_RENDER_DRAW_COLOR(renderer, &marker);
			SDL_RenderDrawLineF(renderer, x, (float)plot->y, x, (float)(plot->y + plot->h));

			char text[LABEL_TEXT];
			snprintf(text, sizeof(text), "%u", (unsigned)k);
			label_set(&h->numbers[k - 1], renderer, h->font, h->config.major_color, text);
			label_blit(&h->numbers[k - 1], renderer, x, (float)(plot->y + plot->h + LABEL_MARGIN), ANCHOR_TOP_MIDDLE);

			uint32_t search = (uint32_t)ceil(fk * SEARCH_RATIO / delta_f);
			if (search < SEARCH_MIN_BINS) search = SEARCH_MIN_BINS;
			harmonic_readout_t r = harmonic_readout(h->db, bins, delta_f, fk, search, shoulder, PROMINENCE_DB, &h->window);
			if (r.found) snprintf(text, sizeof(text), "%.1f", r.db);
			else text[0] = '\0';
			label_set(&h->readouts[k - 1], renderer, h->font, h->config.readout_color, text);
			label_blit(&h->readouts[k - 1], renderer, x, (float)(plot->y + LABEL_MARGIN), ANCHOR_TOP_MIDDLE);
		}
	}

	// the fundamental
	char header[LABEL_TEXT];
	if (f0 > 0.0) snprintf(header, sizeof(header), "%.1f Hz%s", f0, h->pitch_valid ? "" : " (held)");
	else snprintf(header, sizeof(header), "no pitch");
	label_set(&h->header, renderer, h->font, h->pitch_valid ? h->config.readout_color : h->config.held_color, header);
	label_blit(&h->header, renderer, (float)plot->x, (float)(plot->y - LABEL_MARGIN), ANCHOR_BOTTOM_LEFT);
}

void harmonics_render(harmonics_t *h, SDL_Renderer *renderer) {
	if (!fft_analyzer_streaming(h->analyzer)) return;
	apply_config(h);
	prepare_to_draw(h, renderer);
	if (!h->graticule || !h->plot) return;

	fft_analyzer_update(h->analyzer);
	if (!fft_analyzer_streaming(h->analyzer)) return;   // a failed reconfiguration ends the stream
	if (analyze(h) != 0) return;
	draw(h, renderer);
}
