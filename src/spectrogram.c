#include "s9.h"
#include "spectrogram.h"
#include "fft_analyzer.h"
#include "fresp_graph.h"

typedef struct spectrogram {
	SDL_Renderer *renderer;
	SDL_Rect viewport;
	spectrogram_config_t config;

	fft_analyzer_t *analyzer;

	uint32_t n_graphs;
	fresp_graph_t **graphs;
} spectrogram_t;

void spectrogram_set_config(spectrogram_t *sg, const spectrogram_config_t *config) {
	if (config == NULL) {
		sg->config = default_spectrogram_config;
	} else {
		sg->config = *config;
	}
}

spectrogram_t *spectrogram_new(const spectrogram_config_t *config) {
	spectrogram_t *sg = (spectrogram_t *) malloc(sizeof(spectrogram_t));
	CHECK_ALLOC(sg, return NULL);
	memset(sg, 0, sizeof(*sg));

	spectrogram_set_config(sg, config);

	sg->analyzer = fft_analyzer_new(&(fft_analyzer_config_t){
		.resolution_hz = sg->config.resolution_hz,
		.window_function = sg->config.window_function,
		.window_skew = sg->config.window_skew,
		.mode = FFT_ANALYZER_EACH_CHANNEL,
	});
	CHECK_ALLOC(sg->analyzer, free(sg); return NULL);

	return sg;
}

static void free_graphs(spectrogram_t *sg) {
	if( sg->n_graphs ) {
		for(uint32_t i=0; i < sg->n_graphs; i++) {
			fresp_graph_free(&(sg->graphs[i]));
		}
		sg->n_graphs = 0;

		free(sg->graphs);
		sg->graphs = NULL;
	}
}

void spectrogram_free(spectrogram_t **sg) {
	if(sg && *sg) {
		fft_analyzer_free(&(*sg)->analyzer);
		free_graphs(*sg);
		free(*sg);
		*sg = NULL;
	}
}

static S9_FORCE_INLINE void prepare_to_draw(spectrogram_t *sg, SDL_Renderer *renderer) {
	SDL_Rect viewport;
	SDL_RenderGetViewport(renderer, &viewport);

	if(sg->renderer != renderer || !SDL_RectEquals(&sg->viewport, &viewport)) {
		sg->renderer = renderer;
		sg->viewport = viewport;

		free_graphs(sg);

		uint32_t n = fft_analyzer_transforms(sg->analyzer);
		if(n) {
			sg->graphs = malloc(sizeof(fresp_graph_t*) * n);
			CHECK_ALLOC(sg->graphs, return);
			memset(sg->graphs, 0, sizeof(fresp_graph_t*) * n);

			for(uint32_t i=0; i < n; i++) {
				sg->graphs[i] = fresp_graph_new(&sg->config.graph_config);
			}

			sg->n_graphs = n;
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
	spectrogram_t *sg;
	uint32_t channel;
} fresp_graph_data_callback_context_t;

static int fresp_graph_data_callback(void *context, float *plot_data, uint32_t n_points) {
	fresp_graph_data_callback_context_t *callback_context = (fresp_graph_data_callback_context_t*)context;

	spectrogram_t *sg = callback_context->sg;
	fft_analyzer_t *fa = sg->analyzer;

	compute_plot_data(fft_analyzer_spectrum(fa, callback_context->channel), fft_analyzer_fft_len(fa), fft_analyzer_sample_rate(fa),
					  sg->config.graph_config.min_freq, sg->config.graph_config.max_freq, plot_data, n_points);

	return 0;
}

void spectrogram_render(spectrogram_t *sg, SDL_Renderer *renderer) {
	if(!fft_analyzer_streaming(sg->analyzer)) return;
	prepare_to_draw(sg, renderer);

	fft_analyzer_update(sg->analyzer);

	SDL_Rect save_viewport, graph_viewport;
	SDL_RenderGetViewport(renderer, &save_viewport);

	graph_viewport = save_viewport;
	graph_viewport.h = save_viewport.h / sg->n_graphs;

	fresp_graph_data_callback_context_t callback_context = { .sg = sg };

	for(uint32_t i = 0; i < sg->n_graphs; i++) {
		SDL_RenderSetViewport(renderer, &graph_viewport);
		callback_context.channel = i;
		fresp_graph_render(sg->graphs[i], renderer, fresp_graph_data_callback, &callback_context);

		graph_viewport.y += graph_viewport.h;
	}

	SDL_RenderSetViewport(renderer, &save_viewport);
}

void spectrogram_stop_streaming(spectrogram_t *sg) {
	fft_analyzer_stop_streaming(sg->analyzer);
	sg->renderer = NULL;
}

void spectrogram_prepare_to_stream(spectrogram_t *sg, const s9_audio_stream_config_t *stream_config) {
	fft_analyzer_prepare_to_stream(sg->analyzer, stream_config);
}

void spectrogram_enqueue_audio_data(spectrogram_t *sg, const void *data, uint32_t size) {
	fft_analyzer_enqueue_audio_data(sg->analyzer, data, size);
}
