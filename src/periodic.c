#include "periodic.h"
#include "s9.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct periodic {
	periodic_config_t config;
	int config_changed;              // applied at the next frame

	uint32_t sample_rate;            // of the stream; 0 before streaming
	fft_analyzer_t *feed;            // the tracker's audio: the channel mixed, a hop at a time, untransformed
	tracker_t *tracker;              // made at the stream's rate; NULL before streaming
	tracker_config_t tracker_made;   // the configuration it was made with
	frequency_counter_t *counter;    // likewise
	frequency_counter_config_t counter_made;
	fft_analyzer_t *spectrum;        // the harmonics view's transform
	harmonics_t *harmonics;
	oscilloscope_t *waveform;
	note_strip_t *range;
	tuner_strip_t *tuner;
	int waveform_fed;                // audio goes to the waveform's sampler only while it is the view:
	                                 // nothing drains it otherwise, and it would overflow

	tracker_state_t state;           // as of the last hop
	bool valid;                      // the fundamental is decided and its lock held
	float fundamental;               // the last decided fundamental, held while invalid; 0 before any
};

periodic_config_t periodic_default_config(void) {
	periodic_config_t c = {
		.channel = S9_CHANNEL_ALL,
		.view = PERIODIC_VIEW_HARMONICS,
		.initial_f0 = 1000.0f,
		.tracker = default_tracker_config,
		.counter = default_frequency_counter_config,
		.spectrum = { .window_s = 0.75f, .window_function = WINDOW_FUNCTION_BLACKMAN_HARRIS, .averaging_s = 1.0f },
		.harmonics = default_harmonics_config,
		.waveform = { .display = default_oscilloscope_config, .cycles = 1, .amp_per_div = 0.2f, .lock_cycles = 2, .lock_time = 0.3f },
		.note = { .a4 = 440.0f, .range = default_note_strip_config, .tuner = default_tuner_strip_config },
	};
	return c;
}

const periodic_config_t *periodic_config(const periodic_t *p) {
	return &p->config;
}

void periodic_set_config(periodic_t *p, const periodic_config_t *config) {
	p->config = config ? *config : periodic_default_config();
	if ((unsigned)p->config.view >= PERIODIC_VIEW_COUNT) p->config.view = PERIODIC_VIEW_HARMONICS;
	if (!(p->config.initial_f0 > 0.0f)) p->config.initial_f0 = periodic_default_config().initial_f0;
	if (!(p->config.spectrum.window_s > 0.0f)) p->config.spectrum.window_s = periodic_default_config().spectrum.window_s;
	if (p->config.waveform.cycles < 1) p->config.waveform.cycles = 1;
	if (p->config.waveform.cycles > PERIODIC_CYCLES_MAX) p->config.waveform.cycles = PERIODIC_CYCLES_MAX;
	if (p->config.waveform.lock_cycles < 1) p->config.waveform.lock_cycles = 1;
	if (!(p->config.note.a4 > 0.0f)) p->config.note.a4 = periodic_default_config().note.a4;
	p->config_changed = 1;
}

// the tracker's hop at the stream's rate, in samples
static uint32_t hop_samples(const periodic_t *p) {
	uint32_t hop = (uint32_t)lrint((double)p->config.tracker.hop_s * p->sample_rate);
	return hop < 1 ? 1 : hop;
}

// the feed: the channel mixed to one, exactly a hop consumed per update, no transform
static fft_analyzer_config_t feed_config(const periodic_t *p) {
	uint32_t hop = p->sample_rate ? hop_samples(p) : 0;
	return (fft_analyzer_config_t){
		.fft_len = hop,
		.resolution_hz = 1000.0f,   // unused once the length is given
		.window_function = WINDOW_FUNCTION_RECTANGULAR,
		.window_skew = 0.0f,
		.mode = FFT_ANALYZER_MIX,
		.channel = p->config.channel,
		.averaging_s = 0.0f,
		.hop_frames = hop,
		.frame_only = 1,
		.pad = 0,
	};
}

// the longest power of two of samples within the harmonics view's window
static uint32_t spectrum_points(const periodic_t *p) {
	uint32_t points = 2;
	while (points * 2 <= p->config.spectrum.window_s * (float)p->sample_rate) points *= 2;
	return points;
}

static fft_analyzer_config_t spectrum_config(const periodic_t *p) {
	return (fft_analyzer_config_t){
		.fft_len = p->sample_rate ? spectrum_points(p) : 0,
		.resolution_hz = 1.0f,      // unused once the length is given
		.window_function = p->config.spectrum.window_function,
		.window_skew = 0.0f,
		.mode = FFT_ANALYZER_MIX,
		.channel = p->config.channel,
		.averaging_s = p->config.spectrum.averaging_s,
		.hop_frames = 0,
		.frame_only = 0,
		.pad = 0,
	};
}

// the harmonics view's configuration, on this component's assumed fundamental
static harmonics_config_t harmonics_config_of(const periodic_t *p) {
	harmonics_config_t c = p->config.harmonics;
	c.initial_f0 = p->config.initial_f0;
	return c;
}

// the waveform view: this component's channel, each sweep starting as the
// fundamental crosses zero rising, so whole cycles fill it however the
// harmonics shape the waveform, and no free-running sweep, so the trace
// holds when the signal stops
static void apply_waveform_settings(periodic_t *p) {
	oscilloscope_set_config(p->waveform, &p->config.waveform.display);
	oscilloscope_set_channel(p->waveform, p->config.channel);
	oscilloscope_set_amp_per_div(p->waveform, p->config.waveform.amp_per_div);
	oscilloscope_set_trig_auto(p->waveform, false);
	oscilloscope_set_trig_slope(p->waveform, 1);
	oscilloscope_set_trig_holdoff(p->waveform, 0.0f);
	oscilloscope_set_trig_lock_cycles(p->waveform, p->config.waveform.lock_cycles);
	oscilloscope_set_trig_lock_time(p->waveform, p->config.waveform.lock_time);
}

// a tracker at the stream's rate from the configuration, in place of any before; it starts over
static void make_tracker(periodic_t *p) {
	tracker_free(&p->tracker);
	if (!p->sample_rate) return;
	p->tracker = tracker_new(&p->config.tracker, p->sample_rate);
	CHECK_ALLOC(p->tracker, return);
	p->tracker_made = p->config.tracker;
	memset(&p->state, 0, sizeof p->state);
	p->valid = false;
}

// a counter at the stream's rate from the configuration, in place of any before
static void make_counter(periodic_t *p) {
	frequency_counter_free(&p->counter);
	if (!p->sample_rate) return;
	p->counter = frequency_counter_new(&p->config.counter, p->sample_rate);
	CHECK_ALLOC(p->counter, return);
	p->counter_made = p->config.counter;
}

// hand a changed configuration to the tracker, the counter, the transforms and the views
static void apply_config(periodic_t *p) {
	if (!p->config_changed) return;
	p->config_changed = 0;
	if (p->sample_rate && (!p->tracker || memcmp(&p->tracker_made, &p->config.tracker, sizeof p->config.tracker) != 0))
		make_tracker(p);
	if (p->sample_rate && (!p->counter || memcmp(&p->counter_made, &p->config.counter, sizeof p->config.counter) != 0))
		make_counter(p);
	fft_analyzer_config_t fc = feed_config(p);
	fft_analyzer_set_config(p->feed, &fc);
	fft_analyzer_config_t sc = spectrum_config(p);
	fft_analyzer_set_config(p->spectrum, &sc);
	harmonics_config_t hc = harmonics_config_of(p);
	harmonics_set_config(p->harmonics, &hc);
	apply_waveform_settings(p);
	note_strip_set_config(p->range, &p->config.note.range);
	tuner_strip_set_config(p->tuner, &p->config.note.tuner);
	p->waveform_fed = p->config.view == PERIODIC_VIEW_WAVEFORM;
}

periodic_t *periodic_new(const periodic_config_t *config) {
	periodic_t *p = malloc(sizeof(periodic_t));
	CHECK_ALLOC(p, return NULL);
	memset(p, 0, sizeof(*p));

	periodic_set_config(p, config);
	p->config_changed = 0;

	fft_analyzer_config_t fc = feed_config(p);
	p->feed = fft_analyzer_new(&fc);
	CHECK_ALLOC(p->feed, periodic_free(&p); return NULL);
	fft_analyzer_config_t sc = spectrum_config(p);
	p->spectrum = fft_analyzer_new(&sc);
	CHECK_ALLOC(p->spectrum, periodic_free(&p); return NULL);
	harmonics_config_t hc = harmonics_config_of(p);
	p->harmonics = harmonics_new(&hc, p->spectrum);
	CHECK_ALLOC(p->harmonics, periodic_free(&p); return NULL);
	p->waveform = oscilloscope_new(&p->config.waveform.display);
	CHECK_ALLOC(p->waveform, periodic_free(&p); return NULL);
	p->range = note_strip_new(&p->config.note.range);
	CHECK_ALLOC(p->range, periodic_free(&p); return NULL);
	p->tuner = tuner_strip_new(&p->config.note.tuner);
	CHECK_ALLOC(p->tuner, periodic_free(&p); return NULL);
	apply_waveform_settings(p);
	p->waveform_fed = p->config.view == PERIODIC_VIEW_WAVEFORM;
	return p;
}

void periodic_free(periodic_t **p) {
	if (p && *p) {
		tuner_strip_free(&(*p)->tuner);
		note_strip_free(&(*p)->range);
		oscilloscope_free(&(*p)->waveform);
		harmonics_free(&(*p)->harmonics);
		fft_analyzer_free(&(*p)->spectrum);
		frequency_counter_free(&(*p)->counter);
		tracker_free(&(*p)->tracker);
		fft_analyzer_free(&(*p)->feed);
		free(*p);
		*p = NULL;
	}
}

bool periodic_valid(const periodic_t *p) { return p->valid; }
float periodic_frequency(const periodic_t *p) { return p->fundamental; }
float periodic_harmonicity(const periodic_t *p) { return p->state.harmonicity; }

bool periodic_lock(const periodic_t *p, float *frequency, float *confidence) {
	*frequency = p->state.locked ? p->state.lock_hz : 0.0f;
	*confidence = p->state.locked ? p->state.lock_confidence : 0.0f;
	return p->state.locked;
}

bool periodic_counter(const periodic_t *p, float *frequency) {
	if (!p->counter) { *frequency = 0.0f; return false; }
	return frequency_counter_read(p->counter, frequency);
}

bool periodic_note(const periodic_t *p, note_t *note) {
	if (!(p->fundamental > 0.0f)) return false;
	*note = note_from_frequency(p->fundamental, p->config.note.a4);
	return true;
}

const harmonic_reading_t *periodic_readings(const periodic_t *p, uint32_t *count) {
	return harmonics_readings(p->harmonics, count);
}

int periodic_thd(const periodic_t *p, float *thd) {
	return harmonics_get_thd(p->harmonics, thd);
}

// -- streaming

void periodic_prepare_to_stream(periodic_t *p, const s9_audio_stream_config_t *stream_config) {
	p->sample_rate = stream_config->sample_rate;
	fft_analyzer_config_t fc = feed_config(p);
	fft_analyzer_set_config(p->feed, &fc);
	fft_analyzer_prepare_to_stream(p->feed, stream_config);
	fft_analyzer_config_t sc = spectrum_config(p);
	fft_analyzer_set_config(p->spectrum, &sc);
	fft_analyzer_prepare_to_stream(p->spectrum, stream_config);
	make_tracker(p);
	make_counter(p);
	p->fundamental = 0.0f;
	harmonics_prepare_to_stream(p->harmonics, stream_config);
	oscilloscope_prepare_to_stream(p->waveform, stream_config);
}

void periodic_stop_streaming(periodic_t *p) {
	oscilloscope_stop_streaming(p->waveform);
	harmonics_stop_streaming(p->harmonics);
	fft_analyzer_stop_streaming(p->spectrum);
	fft_analyzer_stop_streaming(p->feed);
}

void periodic_enqueue_audio_data(periodic_t *p, const void *data, uint32_t size) {
	fft_analyzer_enqueue_audio_data(p->feed, data, size);
	fft_analyzer_enqueue_audio_data(p->spectrum, data, size);
	if (p->waveform_fed) oscilloscope_enqueue_audio_data(p->waveform, data, size);
}

// -- the frame

// every hop queued into the tracker and the counter, and the transform's update
static void track(periodic_t *p) {
	if (!p->tracker) return;
	uint32_t hop = tracker_hop(p->tracker);
	while (fft_analyzer_update(p->feed) > 0) {
		const float *frame = fft_analyzer_frame(p->feed, 0);
		uint32_t len = fft_analyzer_fft_len(p->feed);
		if (!frame || len < hop) break;
		tracker_feed(p->tracker, frame + (len - hop), hop);
		if (p->counter) frequency_counter_feed(p->counter, frame + (len - hop), hop);
	}
	p->state = tracker_state(p->tracker);
	p->valid = p->state.committed;
	if (p->valid) p->fundamental = p->state.fundamental_hz;
	fft_analyzer_update(p->spectrum);
}

void periodic_render(periodic_t *p, SDL_Renderer *renderer) {
	if (!fft_analyzer_streaming(p->feed)) return;
	apply_config(p);
	track(p);

	// the note across the top, on the range tracked and then on the tuner's scale; the view under them
	SDL_Rect viewport;
	SDL_RenderGetViewport(renderer, &viewport);
	int top = 2 * STRIP_HEIGHT;
	SDL_Rect range_band = { viewport.x, viewport.y, viewport.w, STRIP_HEIGHT };
	SDL_Rect tuner_band = { viewport.x, viewport.y + STRIP_HEIGHT, viewport.w, STRIP_HEIGHT };
	SDL_Rect below = { viewport.x, viewport.y + top, viewport.w, viewport.h - top };
	note_t note, lock;
	bool have_note = periodic_note(p, &note);
	bool have_lock = p->state.locked && p->state.lock_hz > 0.0f;
	if (have_lock) lock = note_from_frequency(p->state.lock_hz, p->config.note.a4);
	float f_min = p->config.tracker.f_min, f_max = p->config.tracker.f_max, a4 = p->config.note.a4;
	float low = 0.0f, high = 0.0f;
	if (f_min > 0.0f && f_max > f_min) {
		note_t lowest = note_from_frequency(f_min, a4), highest = note_from_frequency(f_max, a4);
		low = note_midi(&lowest);
		high = note_midi(&highest);
	}
	SDL_RenderSetViewport(renderer, &range_band);
	note_strip_render(p->range, renderer, low, high, have_note ? &note : NULL, p->fundamental, p->valid, have_lock ? &lock : NULL);
	SDL_RenderSetViewport(renderer, &tuner_band);
	tuner_strip_render(p->tuner, renderer, have_note ? &note : NULL, p->valid);
	if (below.h < 2) {
		SDL_RenderSetViewport(renderer, &viewport);
		return;
	}
	SDL_RenderSetViewport(renderer, &below);
	switch (p->config.view) {
	case PERIODIC_VIEW_HARMONICS:
		harmonics_render(p->harmonics, renderer, p->fundamental, p->valid);
		break;
	case PERIODIC_VIEW_WAVEFORM: {
		// the sweep spans whole cycles of the fundamental, the last decided or the one assumed
		float f0 = p->fundamental;
		if (!(f0 > 0.0f)) f0 = p->config.initial_f0;
		oscilloscope_set_time_per_div(p->waveform, p->config.waveform.cycles / f0 / OSCILLOSCOPE_DIVISIONS);
		oscilloscope_set_trig_lock(p->waveform, f0);
		oscilloscope_render(p->waveform, renderer);
		break;
	}
	default:
		break;
	}
	SDL_RenderSetViewport(renderer, &viewport);
}
