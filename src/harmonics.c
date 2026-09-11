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
#define PEAK_TOLERANCE 0.02f    // how far below the highest autocorrelation peak an earlier one may be
                                // and still be the period
#define AXIS_HEADROOM 0.5f      // the axis extends this many fundamentals past the last marker
#define LABEL_TEXT 64

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
	harmonic_reader_t *reader;
	uint32_t buffer_len;             // the transform length nacf, db, and live_db are sized for
	float *nacf;
	float *db;                       // the averaged spectrum, per bin: what the readings come from
	float *live_db;                  // the latest transform's, per bin: what the curve shows
	uint32_t reader_window_len;      // the transform length the reader's window is; 0 before any

	bool pitch_valid;
	float pitch_frequency;

	SDL_Renderer *renderer;
	SDL_Rect viewport;
	TTF_Font *font;
	SDL_Rect plot_rect;
	SDL_Texture *graticule;
	float *plot;                     // dB per plot column
	SDL_FPoint *points;
	label_t readouts[HARMONIC_MAX];  // the level read at each harmonic
	label_t degrees[HARMONIC_MAX];   // and its phase, under that
	label_t numbers[HARMONIC_MAX];   // the harmonic numbers under the markers
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
		.averaging_s = h->config.averaging_s,
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
	harmonics_config_t before = h->config;
	h->config = config ? *config : default_harmonics_config;
	if (h->config.resolution_hz != before.resolution_hz || h->config.window_function != before.window_function)
		h->reader_window_len = 0;   // the analyzer's window changes with them; the reader's follows
	if (h->config.reader.harmonics < 1) h->config.reader.harmonics = 1;
	if (h->config.reader.harmonics > HARMONIC_MAX) h->config.reader.harmonics = HARMONIC_MAX;
	if (!(h->config.resolution_hz > 0.0f)) h->config.resolution_hz = default_harmonics_config.resolution_hz;
	if (!(h->config.plot_width >= 1.0f)) h->config.plot_width = default_harmonics_config.plot_width;
	h->config_changed = 1;
}

// hand a changed configuration to the analyzer, the conditioner, and the reader
static void apply_config(harmonics_t *h) {
	if (!h->config_changed) return;
	h->config_changed = 0;

	fft_analyzer_config_t ac = analyzer_config(h);
	fft_analyzer_set_config(h->analyzer, &ac);
	pitch_conditioner_set_config(h->conditioner, &h->config.tracker);
	harmonic_reader_set_config(h->reader, &h->config.reader);
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
	for (int i = 0; i < HARMONIC_MAX; i++) {
		free_label(&h->readouts[i]);
		free_label(&h->degrees[i]);
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
	pitch_conditioner_init(h->conditioner, &h->config.tracker);

	h->reader = malloc(harmonic_reader_alloc_size());
	CHECK_ALLOC(h->reader, harmonics_free(&h); return NULL);
	harmonic_reader_init(h->reader, &h->config.reader);
	return h;
}

void harmonics_free(harmonics_t **h) {
	if (h && *h) {
		free_drawing(*h);
		fft_analyzer_free(&(*h)->analyzer);
		free((*h)->conditioner);
		free((*h)->reader);
		free((*h)->nacf);
		free((*h)->db);
		free((*h)->live_db);
		free(*h);
		*h = NULL;
	}
}

bool harmonics_get_pitch_valid(const harmonics_t *h) { return h->pitch_valid; }
float harmonics_get_pitch_frequency(const harmonics_t *h) { return h->pitch_frequency; }

const harmonic_reading_t *harmonics_readings(const harmonics_t *h, uint32_t *count) {
	return harmonic_reader_readings(h->reader, count);
}

int harmonics_get_thd(const harmonics_t *h, float *thd) {
	return harmonic_reader_thd(h->reader, thd);
}

// -- streaming

void harmonics_prepare_to_stream(harmonics_t *h, const s9_audio_stream_config_t *stream_config) {
	fft_analyzer_prepare_to_stream(h->analyzer, stream_config);
	pitch_conditioner_reset(h->conditioner);
	harmonic_reader_reset(h->reader);
	h->pitch_valid = false;
	h->pitch_frequency = 0.0f;
	h->reader_window_len = 0;
}

void harmonics_stop_streaming(harmonics_t *h) {
	fft_analyzer_stop_streaming(h->analyzer);
	h->renderer = NULL;
}

void harmonics_enqueue_audio_data(harmonics_t *h, const void *data, uint32_t size) {
	fft_analyzer_enqueue_audio_data(h->analyzer, data, size);
}

// -- analysis

// the axis' fundamental: the tracker's, or the configured one before it has locked
static double axis_f0(const harmonics_t *h) {
	return h->pitch_frequency > 0.0f ? h->pitch_frequency : h->config.initial_f0;
}

// the axis' right edge: the harmonics across, within the band
static double axis_end(const harmonics_t *h, double f0, double fs) {
	double f_end = (h->config.reader.harmonics + AXIS_HEADROOM) * f0;
	return f_end > fs / 2.0 ? fs / 2.0 : f_end;
}

// the fundamental from the latest transform, and the harmonics read from
// the averaged spectrum
static int analyze(harmonics_t *h) {
	fft_analyzer_t *fa = h->analyzer;
	uint32_t n = fft_analyzer_fft_len(fa), bins = n / 2 + 1;
	float fs = (float)fft_analyzer_sample_rate(fa);

	if (h->buffer_len != n) {
		free(h->nacf); free(h->db); free(h->live_db);
		h->nacf = malloc(sizeof(float) * n);
		h->db = malloc(sizeof(float) * bins);
		h->live_db = malloc(sizeof(float) * bins);
		h->buffer_len = h->nacf && h->db && h->live_db ? n : 0;
		CHECK_ALLOC(h->nacf, return -1);
		CHECK_ALLOC(h->db, return -1);
		CHECK_ALLOC(h->live_db, return -1);
	}
	if (h->reader_window_len != n) {
		harmonic_reader_set_window(h->reader, fft_analyzer_window(fa), n);
		h->reader_window_len = n;
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
	float dt = fft_analyzer_hop_seconds(fa);
	h->pitch_valid = pitch_conditioner_update(h->conditioner, e.period, e.clarity, rms, fs, dt, &h->pitch_frequency);

	// the curve shows the latest transform; the harmonics are read from the average
	const float *spectrum = fft_analyzer_spectrum(fa, 0);
	harmonic_magnitude_db(spectrum, bins, n, h->live_db);
	harmonic_power_db(fft_analyzer_power(fa, 0), bins, n, h->db);
	harmonic_reader_update(h->reader, h->db, spectrum, n, fs / n, h->pitch_frequency, h->pitch_valid,
						   fft_analyzer_average_fraction(fa), dt);
	if (harmonic_reader_restarted(h->reader)) fft_analyzer_reset_power(fa);   // a new note: the average too
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

	// lines go into the texture as they are, alpha included, so they blend
	// onto the screen once, exactly as the markers drawn there do
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	graph_db_axis_draw(&axis, renderer, h->font, &h->plot_rect, LABEL_MARGIN);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

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

// the latest spectrum from 0 to f_end Hz across the plot columns: the
// loudest bin in each column, or the level between bins where a column
// holds none
static void reduce_plot(harmonics_t *h, double f_end, double delta_f, uint32_t bins) {
	const float *db = h->live_db;
	int w = h->plot_rect.w;
	float floor_db = (float)h->config.min_db - 10.0f;   // off the bottom of the plot
	for (int i = 0; i < w; i++) {
		double lo = f_end * i / w, hi = f_end * (i + 1) / w;
		uint32_t b0 = (uint32_t)ceil(lo / delta_f);
		uint32_t b1 = (uint32_t)ceil(hi / delta_f);   // one past the last bin below hi
		if (b1 > bins) b1 = bins;
		float v;
		if (b0 < b1) {
			v = db[b0];
			for (uint32_t b = b0 + 1; b < b1; b++)
				if (db[b] > v) v = db[b];
		} else {
			double f = 0.5 * (lo + hi) / delta_f;
			uint32_t b = (uint32_t)f;
			if (b + 1 >= bins) {
				v = db[bins - 1];
			} else {
				float t = (float)(f - b);
				v = isfinite(db[b]) && isfinite(db[b + 1]) ? db[b] + t * (db[b + 1] - db[b]) : floor_db;
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
	double f0 = axis_f0(h);
	double f_end = axis_end(h, f0, fs);

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
	sdl_path_t *path = sdl_path_new(&h->config.plot_color, h->config.plot_width, 1, h->points, plot->w);
	if (path) {
		sdl_path_render(path, renderer);
		sdl_path_free(&path);
	}
	SDL_RenderSetViewport(renderer, &save_viewport);

	SDL_Rect dst = { 0, 0, h->viewport.w, h->viewport.h };
	SDL_RenderCopy(renderer, h->graticule, NULL, &dst);

	// markers and numbers at the harmonics on the axis
	uint32_t count = h->config.reader.harmonics;
	char text[LABEL_TEXT];
	SDL_SET_RENDER_DRAW_COLOR(renderer, &h->config.minor_color);
	for (uint32_t k = 1; k <= count; k++) {
		double fk = k * f0;
		if (fk > f_end) break;
		float x = plot->x + (float)(plot->w * fk / f_end);
		SDL_RenderDrawLineF(renderer, x, (float)plot->y, x, (float)(plot->y + plot->h));
		snprintf(text, sizeof(text), "%u", (unsigned)k);
		label_set(&h->numbers[k - 1], renderer, h->font, h->config.major_color, text);
		label_blit(&h->numbers[k - 1], renderer, x, (float)(plot->y + plot->h + LABEL_MARGIN), ANCHOR_TOP_MIDDLE);
	}

	// the levels and phases relative to the fundamental's, while locked and the fundamental read
	uint32_t read_count;
	const harmonic_reading_t *reads = harmonic_reader_readings(h->reader, &read_count);
	const harmonic_reading_t *h1 = read_count && reads[0].shown ? &reads[0] : NULL;
	for (uint32_t k = 1; k <= count; k++) {
		const harmonic_reading_t *r = k <= read_count ? &reads[k - 1] : NULL;
		char degrees[LABEL_TEXT];
		if (h1 && r && r->shown) {
			snprintf(text, sizeof(text), "%.1f", r->readout.db - h1->readout.db);
			int deg = (int)lrintf(r->phase_deg);
			if (deg == 0) snprintf(degrees, sizeof(degrees), "0\xc2\xb0");
			else snprintf(degrees, sizeof(degrees), "%+d\xc2\xb0", deg);
		} else {
			text[0] = '\0';
			degrees[0] = '\0';
		}
		label_set(&h->readouts[k - 1], renderer, h->font, h->config.readout_color, text);
		label_set(&h->degrees[k - 1], renderer, h->font, h->config.readout_color, degrees);
		if (k * f0 <= f_end) {
			float x = plot->x + (float)(plot->w * k * f0 / f_end);
			label_blit(&h->readouts[k - 1], renderer, x, (float)(plot->y + LABEL_MARGIN), ANCHOR_TOP_MIDDLE);
			label_blit(&h->degrees[k - 1], renderer, x, (float)(plot->y + LABEL_MARGIN + h->readouts[k - 1].h), ANCHOR_TOP_MIDDLE);
		}
	}

	// the fundamental: its level and the distortion while locked and read
	if (!h->pitch_valid) snprintf(text, sizeof(text), "%.1f Hz   H1 not found", f0);
	else if (!h1) snprintf(text, sizeof(text), "%.1f Hz", f0);
	else {
		float thd = 0.0f;
		harmonic_reader_thd(h->reader, &thd);
		snprintf(text, sizeof(text), "%.1f Hz   H1 %.1f dB   THD %.3f %%", f0, h1->readout.db, 100.0 * thd);
	}
	label_set(&h->header, renderer, h->font, h->pitch_valid ? h->config.readout_color : h->config.held_color, text);
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
