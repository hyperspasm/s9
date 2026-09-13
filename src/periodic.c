#include "periodic.h"
#include "s9.h"

#include <stdlib.h>

struct periodic {
	periodic_config_t config;
	int config_changed;              // applied at the next frame

	fundamental_t *tracker;
	harmonics_t *harmonics;
	oscilloscope_t *waveform;
	note_strip_t *strip;
	int waveform_fed;                // audio goes to the waveform's sampler only while it is the view:
	                                 // nothing drains it otherwise, and it would overflow
};

periodic_config_t periodic_default_config(void) {
	periodic_config_t c = {
		.channel = S9_CHANNEL_ALL,
		.view = PERIODIC_VIEW_HARMONICS,
		.initial_f0 = 1000.0f,
		.tracker = default_fundamental_config,
		.harmonics = default_harmonics_config,
		.waveform = { .display = default_oscilloscope_config, .cycles = 1, .amp_per_div = 0.2f, .lock_cycles = 2, .lock_time = 0.3f },
		.note = { .a4 = 440.0f, .strip = default_note_strip_config },
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
	if (p->config.waveform.cycles < 1) p->config.waveform.cycles = 1;
	if (p->config.waveform.cycles > PERIODIC_CYCLES_MAX) p->config.waveform.cycles = PERIODIC_CYCLES_MAX;
	if (p->config.waveform.lock_cycles < 1) p->config.waveform.lock_cycles = 1;
	if (!(p->config.note.a4 > 0.0f)) p->config.note.a4 = periodic_default_config().note.a4;
	p->config_changed = 1;
}

// the tracker's configuration, on this component's channel
static fundamental_config_t tracker_config(const periodic_t *p) {
	fundamental_config_t c = p->config.tracker;
	c.channel = p->config.channel;
	return c;
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

// hand a changed configuration to the tracker and the views
static void apply_config(periodic_t *p) {
	if (!p->config_changed) return;
	p->config_changed = 0;
	fundamental_config_t tc = tracker_config(p);
	fundamental_set_config(p->tracker, &tc);
	harmonics_config_t hc = harmonics_config_of(p);
	harmonics_set_config(p->harmonics, &hc);
	apply_waveform_settings(p);
	note_strip_set_config(p->strip, &p->config.note.strip);
	p->waveform_fed = p->config.view == PERIODIC_VIEW_WAVEFORM;
}

periodic_t *periodic_new(const periodic_config_t *config) {
	periodic_t *p = malloc(sizeof(periodic_t));
	CHECK_ALLOC(p, return NULL);
	memset(p, 0, sizeof(*p));

	periodic_set_config(p, config);
	p->config_changed = 0;

	fundamental_config_t tc = tracker_config(p);
	p->tracker = fundamental_new(&tc);
	CHECK_ALLOC(p->tracker, periodic_free(&p); return NULL);
	harmonics_config_t hc = harmonics_config_of(p);
	p->harmonics = harmonics_new(&hc, p->tracker);
	CHECK_ALLOC(p->harmonics, periodic_free(&p); return NULL);
	p->waveform = oscilloscope_new(&p->config.waveform.display);
	CHECK_ALLOC(p->waveform, periodic_free(&p); return NULL);
	p->strip = note_strip_new(&p->config.note.strip);
	CHECK_ALLOC(p->strip, periodic_free(&p); return NULL);
	apply_waveform_settings(p);
	p->waveform_fed = p->config.view == PERIODIC_VIEW_WAVEFORM;
	return p;
}

void periodic_free(periodic_t **p) {
	if (p && *p) {
		note_strip_free(&(*p)->strip);
		oscilloscope_free(&(*p)->waveform);
		harmonics_free(&(*p)->harmonics);
		fundamental_free(&(*p)->tracker);
		free(*p);
		*p = NULL;
	}
}

bool periodic_valid(const periodic_t *p) { return fundamental_valid(p->tracker); }
float periodic_frequency(const periodic_t *p) { return fundamental_frequency(p->tracker); }

bool periodic_note(const periodic_t *p, note_t *note) {
	float f = fundamental_frequency(p->tracker);
	if (!(f > 0.0f)) return false;
	*note = note_from_frequency(f, p->config.note.a4);
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
	fundamental_prepare_to_stream(p->tracker, stream_config);
	harmonics_prepare_to_stream(p->harmonics, stream_config);
	oscilloscope_prepare_to_stream(p->waveform, stream_config);
}

void periodic_stop_streaming(periodic_t *p) {
	oscilloscope_stop_streaming(p->waveform);
	harmonics_stop_streaming(p->harmonics);
	fundamental_stop_streaming(p->tracker);
}

void periodic_enqueue_audio_data(periodic_t *p, const void *data, uint32_t size) {
	fundamental_enqueue_audio_data(p->tracker, data, size);
	if (p->waveform_fed) oscilloscope_enqueue_audio_data(p->waveform, data, size);
}

// -- the frame

void periodic_render(periodic_t *p, SDL_Renderer *renderer) {
	if (!fundamental_streaming(p->tracker)) return;
	apply_config(p);
	fundamental_update(p->tracker);

	// the note across the top, the view under it
	SDL_Rect viewport;
	SDL_RenderGetViewport(renderer, &viewport);
	SDL_Rect band = { viewport.x, viewport.y, viewport.w, NOTE_STRIP_HEIGHT };
	SDL_Rect below = { viewport.x, viewport.y + NOTE_STRIP_HEIGHT, viewport.w, viewport.h - NOTE_STRIP_HEIGHT };
	if (band.h > viewport.h) band.h = viewport.h;
	note_t note;
	bool have_note = periodic_note(p, &note);
	SDL_RenderSetViewport(renderer, &band);
	note_strip_render(p->strip, renderer, have_note ? &note : NULL, fundamental_valid(p->tracker));
	if (below.h < 2) {
		SDL_RenderSetViewport(renderer, &viewport);
		return;
	}
	SDL_RenderSetViewport(renderer, &below);
	switch (p->config.view) {
	case PERIODIC_VIEW_HARMONICS:
		harmonics_render(p->harmonics, renderer);
		break;
	case PERIODIC_VIEW_WAVEFORM: {
		// the sweep spans whole cycles of the fundamental, the last tracked or the one assumed
		float f0 = fundamental_frequency(p->tracker);
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
