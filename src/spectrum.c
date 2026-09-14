#include "s9.h"
#include "spectrum.h"
#include "fft_analyzer.h"
#include "fresp_graph.h"

typedef struct spectrum {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	spectrum_config_t config;

	fft_analyzer_t *analyzer;

	uint32_t n_graphs;
	fresp_graph_t **graphs;
} spectrum_t;

void spectrum_set_config(spectrum_t *sp, const spectrum_config_t *config) {
	if (config == NULL) {
		sp->config = default_spectrum_config;
	} else {
		sp->config = *config;
	}
}

spectrum_t *spectrum_new(const spectrum_config_t *config) {
	spectrum_t *sp = (spectrum_t *) malloc(sizeof(spectrum_t));
	CHECK_ALLOC(sp, return NULL);
	memset(sp, 0, sizeof(*sp));

	spectrum_set_config(sp, config);

	sp->analyzer = fft_analyzer_new(&(fft_analyzer_config_t){
		.resolution_hz = sp->config.resolution_hz,
		.window_function = sp->config.window_function,
		.window_skew = sp->config.window_skew,
		.mode = FFT_ANALYZER_EACH_CHANNEL,
	});
	CHECK_ALLOC(sp->analyzer, free(sp); return NULL);

	return sp;
}

static void free_graphs(spectrum_t *sp) {
	if( sp->n_graphs ) {
		for(uint32_t i=0; i < sp->n_graphs; i++) {
			fresp_graph_free(&(sp->graphs[i]));
		}
		sp->n_graphs = 0;

		free(sp->graphs);
		sp->graphs = NULL;
	}
}

void spectrum_free(spectrum_t **sp) {
	if(sp && *sp) {
		fft_analyzer_free(&(*sp)->analyzer);
		free_graphs(*sp);
		free(*sp);
		*sp = NULL;
	}
}

static S9_FORCE_INLINE void prepare_to_draw(spectrum_t *sp, SDL_Renderer *renderer) {
	SDL_Rect viewport;
	SDL_RenderGetViewport(renderer, &viewport);

	if(sp->renderer != renderer || !SDL_RectEquals(&sp->viewport, &viewport)) {
		sp->renderer = renderer;
		sp->viewport = viewport;

		free_graphs(sp);

		uint32_t n = fft_analyzer_transforms(sp->analyzer);
		if(n) {
			sp->graphs = malloc(sizeof(fresp_graph_t*) * n);
			CHECK_ALLOC(sp->graphs, return);
			memset(sp->graphs, 0, sizeof(fresp_graph_t*) * n);

			for(uint32_t i=0; i < n; i++) {
				sp->graphs[i] = fresp_graph_new(&sp->config.graph_config);
			}

			sp->n_graphs = n;
		}
	}
}

// this function receives a computed complex fft and fills the provided data buffer with dB magnitude values, distributed logarithmically (base 10) between min_freq and max_freq
static void compute_plot_data(const float *fft, uint32_t fft_len, uint32_t sample_rate, uint32_t min_freq, uint32_t max_freq, float *data, uint32_t data_len) {

	// precompute values to optimize for loop
	float bin_size_inv = (float)fft_len / (float)sample_rate;
	float log_min = log10f((float)min_freq);
	float log_max = log10f((float)max_freq);
	float inv_fft_len_half = 2.f / (float)fft_len;
	float inv_fft_len_quarter = inv_fft_len_half * inv_fft_len_half;
	float log_range_inv_data_len = (log_max - log_min) / data_len;
	uint32_t half_fft_len = fft_len / 2;

	for (uint32_t i = 0; i < data_len; i++) {
		// calculate the logarithmic frequency range for the current index
		float log_freq_start = log_min + (i * log_range_inv_data_len);
		float log_freq_end = log_min + ((i + 1) * log_range_inv_data_len);
		float freq_start = powf(10.0f, log_freq_start);
		float freq_end = powf(10.0f, log_freq_end);

		// find the range of FFT bins for the current frequency range
		uint32_t bin_start = (uint32_t)(freq_start * bin_size_inv);
		uint32_t bin_end = (uint32_t)(freq_end * bin_size_inv);

		// limit the bin indices to valid range
		if (bin_start >= half_fft_len) {
			bin_start = half_fft_len - 1;
		}
		if (bin_end >= half_fft_len) {
			bin_end = half_fft_len - 1;
		}

		// find the maximum magnitude within the relevant frequency range
		float max_magnitude = 0.0f;
		for (uint32_t bin = bin_start; bin <= bin_end; bin++) {
			float real = fft[2 * bin];
			float imag = fft[2 * bin + 1];
			float magnitude = real * real + imag * imag; // calculate magnitude squared
			if (magnitude > max_magnitude) {
				max_magnitude = magnitude;
			}
		}

		// normalize by fft_len / 2 and convert magnitude squared to dB
		float max_magnitude_db = 10.0f * log10f(max_magnitude * inv_fft_len_quarter); // since 10*log10(magnitude^2) == 20*log10(magnitude) we can avoid sqrt()

		// store the dB value in the data buffer
		data[i] = max_magnitude_db;
	}
}

typedef struct {
	spectrum_t *sp;
	uint32_t channel;
} fresp_graph_data_callback_context_t;

static int fresp_graph_data_callback(void *context, float *plot_data, uint32_t n_points) {
	fresp_graph_data_callback_context_t *callback_context = (fresp_graph_data_callback_context_t*)context;

	spectrum_t *sp = callback_context->sp;
	fft_analyzer_t *fa = sp->analyzer;

	compute_plot_data(fft_analyzer_spectrum(fa, callback_context->channel), fft_analyzer_fft_len(fa), fft_analyzer_sample_rate(fa),
					  sp->config.graph_config.min_freq, sp->config.graph_config.max_freq, plot_data, n_points);

	return 0;
}

void spectrum_render(spectrum_t *sp, SDL_Renderer *renderer) {
	if(!fft_analyzer_streaming(sp->analyzer)) return;
	prepare_to_draw(sp, renderer);

	fft_analyzer_update(sp->analyzer);

	SDL_Rect save_viewport, graph_viewport;
	SDL_RenderGetViewport(renderer, &save_viewport);

	graph_viewport = save_viewport;
	graph_viewport.h = save_viewport.h / sp->n_graphs;

	fresp_graph_data_callback_context_t callback_context = { .sp = sp };

	for(uint32_t i = 0; i < sp->n_graphs; i++) {
		SDL_RenderSetViewport(renderer, &graph_viewport);
		callback_context.channel = i;
		fresp_graph_render(sp->graphs[i], renderer, fresp_graph_data_callback, &callback_context);

		graph_viewport.y += graph_viewport.h;
	}

	SDL_RenderSetViewport(renderer, &save_viewport);
}

void spectrum_stop_streaming(spectrum_t *sp) {
	fft_analyzer_stop_streaming(sp->analyzer);
	sp->renderer = NULL;
}

void spectrum_prepare_to_stream(spectrum_t *sp, const s9_audio_stream_config_t *stream_config) {
	fft_analyzer_prepare_to_stream(sp->analyzer, stream_config);
}

void spectrum_enqueue_audio_data(spectrum_t *sp, const void *data, uint32_t size) {
	fft_analyzer_enqueue_audio_data(sp->analyzer, data, size);
}
