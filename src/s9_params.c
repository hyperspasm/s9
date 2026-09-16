#include "s9_params.h"
#include "s9_modes.h"
#include "window_function.h"
#include "harmonic_readout.h"
#include "palette.h"
#ifdef PN_SCHEMA_ONLY
// the view names, without the component behind them
typedef enum { PERIODIC_VIEW_HARMONICS, PERIODIC_VIEW_WAVEFORM, PERIODIC_VIEW_COUNT } periodic_view_t;
static const char *const periodic_view_names[PERIODIC_VIEW_COUNT] = { [PERIODIC_VIEW_HARMONICS] = "harmonics", [PERIODIC_VIEW_WAVEFORM] = "waveform" };
#endif

#ifndef PN_SCHEMA_ONLY
#include "s9_app.h"
#include "oscilloscope.h"
#include "stereo_spectrum.h"
#include "periodic.h"
#include "spectrogram.h"

#include <math.h>

// -- app callbacks

static void set_mode(void *ctx, uint32_t val) { (void)ctx; s9_app_set_mode((s9_mode_id_t)val); }
static uint32_t get_mode(void *ctx) { (void)ctx; return (uint32_t)s9_app_get_mode(); }

static void set_fps(void *ctx, bool val) { (void)ctx; s9_app_set_show_fps(val); }
static bool get_fps(void *ctx) { (void)ctx; return s9_app_get_show_fps(); }

// -- oscilloscope callbacks

static void set_time_per_div(void *ctx, float val) { oscilloscope_set_time_per_div(ctx, val); }
static float get_time_per_div(void *ctx) { return oscilloscope_get_time_per_div(ctx); }

static void set_amp_per_div(void *ctx, float val) { oscilloscope_set_amp_per_div(ctx, val); }
static float get_amp_per_div(void *ctx) { return oscilloscope_get_amp_per_div(ctx); }

static void set_trig_auto(void *ctx, bool val) { oscilloscope_set_trig_auto(ctx, val); }
static bool get_trig_auto(void *ctx) { return oscilloscope_get_trig_auto(ctx); }

static void set_trig_slope(void *ctx, int32_t val) { oscilloscope_set_trig_slope(ctx, val); }
static int32_t get_trig_slope(void *ctx) { return oscilloscope_get_trig_slope(ctx); }

static void set_trig_thresh(void *ctx, float val) { oscilloscope_set_trig_thresh(ctx, val); }
static float get_trig_thresh(void *ctx) { return oscilloscope_get_trig_thresh(ctx); }

static void set_trig_holdoff(void *ctx, float val) { oscilloscope_set_trig_holdoff(ctx, val); }
static float get_trig_holdoff(void *ctx) { return oscilloscope_get_trig_holdoff(ctx); }

// channel parameters count from 0, and -1 is every channel
static uint32_t channel_from_param(int32_t val) { return val < 0 ? S9_CHANNEL_ALL : (uint32_t)val; }
static int32_t channel_to_param(uint32_t channel) { return channel == S9_CHANNEL_ALL ? -1 : (int32_t)channel; }

static void set_channel(void *ctx, int32_t val) { oscilloscope_set_channel(ctx, channel_from_param(val)); }
static int32_t get_channel(void *ctx) { return channel_to_param(oscilloscope_get_channel(ctx)); }

// -- spectrum callbacks: the fields of the stereo spectrum's configuration a listener sets

#define SPECTRUM_INT(param, field, type) \
	static void set_##param(void *ctx, int32_t val) { spectrum_config_t c = *stereo_spectrum_config(ctx); c.field = (type)val; stereo_spectrum_set_config(ctx, &c); } \
	static int32_t get_##param(void *ctx) { return (int32_t)stereo_spectrum_config(ctx)->field; }

SPECTRUM_INT(spectrum_resolution, resolution_cents, uint32_t)
SPECTRUM_INT(spectrum_padding, padding, uint32_t)
SPECTRUM_INT(spectrum_f_min, graph_config.min_freq, uint32_t)
SPECTRUM_INT(spectrum_f_max, graph_config.max_freq, uint32_t)
SPECTRUM_INT(spectrum_min_db, graph_config.min_db, int32_t)
SPECTRUM_INT(spectrum_max_db, graph_config.max_db, int32_t)

// -- periodic analyzer callbacks: what it tracks, read only, and each field of its configuration

static float get_fundamental_frequency(void *ctx) { return periodic_frequency(ctx); }
static bool get_fundamental_valid(void *ctx) { return periodic_valid(ctx); }
static float get_fundamental_midi(void *ctx) { note_t note; return periodic_note(ctx, &note) ? note_midi(&note) : 0.0f; }

#define PERIODIC_FLOAT(param, field) \
	static void set_##param(void *ctx, float val) { periodic_config_t c = *periodic_config(ctx); c.field = val; periodic_set_config(ctx, &c); } \
	static float get_##param(void *ctx) { return periodic_config(ctx)->field; }

#define PERIODIC_INT(param, field) \
	static void set_##param(void *ctx, int32_t val) { periodic_config_t c = *periodic_config(ctx); c.field = (uint32_t)val; periodic_set_config(ctx, &c); } \
	static int32_t get_##param(void *ctx) { return (int32_t)periodic_config(ctx)->field; }

PERIODIC_FLOAT(fundamental_window, tracker.window_s)
PERIODIC_FLOAT(fundamental_averaging, tracker.averaging_s)
PERIODIC_FLOAT(fundamental_f_min, tracker.series.f_min)
PERIODIC_FLOAT(fundamental_f_max, tracker.series.f_max)
PERIODIC_FLOAT(fundamental_gate_rms, tracker.conditioner.gate_rms)
PERIODIC_FLOAT(fundamental_harmonicity_on, tracker.conditioner.harmonicity_on)
PERIODIC_FLOAT(fundamental_harmonicity_off, tracker.conditioner.harmonicity_off)
PERIODIC_FLOAT(fundamental_hold, tracker.conditioner.hold_s)
PERIODIC_FLOAT(fundamental_octave_tolerance, tracker.conditioner.octave_tolerance_cents)
PERIODIC_FLOAT(fundamental_smoothing, tracker.conditioner.smoothing_s)
PERIODIC_FLOAT(fundamental_deadband, tracker.conditioner.deadband_cents)
PERIODIC_FLOAT(fundamental_snap, tracker.conditioner.snap_cents)
PERIODIC_INT(harm_harmonics, harmonics.reader.harmonics)
PERIODIC_FLOAT(harm_prominence, harmonics.reader.prominence_db)
PERIODIC_FLOAT(harm_hysteresis, harmonics.reader.hysteresis_db)
PERIODIC_FLOAT(harm_readout_hold, harmonics.reader.hold_s)
PERIODIC_INT(waveform_cycles, waveform.cycles)
PERIODIC_FLOAT(waveform_amp_per_div, waveform.amp_per_div)
PERIODIC_INT(waveform_lock_cycles, waveform.lock_cycles)
PERIODIC_FLOAT(waveform_lock_time, waveform.lock_time)
PERIODIC_FLOAT(note_a4, note.a4)

static void set_periodic_channel(void *ctx, int32_t val) { periodic_config_t c = *periodic_config(ctx); c.channel = channel_from_param(val); periodic_set_config(ctx, &c); }
static int32_t get_periodic_channel(void *ctx) { return channel_to_param(periodic_config(ctx)->channel); }
static void set_periodic_view(void *ctx, uint32_t val) { periodic_config_t c = *periodic_config(ctx); c.view = (periodic_view_t)val; periodic_set_config(ctx, &c); }
static uint32_t get_periodic_view(void *ctx) { return (uint32_t)periodic_config(ctx)->view; }
static void set_fundamental_window_function(void *ctx, uint32_t val) { periodic_config_t c = *periodic_config(ctx); c.tracker.window_function = (window_function_t)val; periodic_set_config(ctx, &c); }
static uint32_t get_fundamental_window_function(void *ctx) { return (uint32_t)periodic_config(ctx)->tracker.window_function; }

// -- spectrogram callbacks: what its settings give, read only, and the fields of its configuration a
// listener sets; the resolution in cents

static float get_spectrogram_time_resolution(void *ctx) { return spectrogram_time_resolution(ctx); }
static float get_spectrogram_floor_frequency(void *ctx) { return spectrogram_floor_frequency(ctx); }

#define SPECTROGRAM_FLOAT(param, field) \
	static void set_##param(void *ctx, float val) { spectrogram_config_t c = *spectrogram_config(ctx); c.field = val; spectrogram_set_config(ctx, &c); } \
	static float get_##param(void *ctx) { return spectrogram_config(ctx)->field; }

#define SPECTROGRAM_INT(param, field) \
	static void set_##param(void *ctx, int32_t val) { spectrogram_config_t c = *spectrogram_config(ctx); c.field = (uint32_t)val; spectrogram_set_config(ctx, &c); } \
	static int32_t get_##param(void *ctx) { return (int32_t)spectrogram_config(ctx)->field; }

SPECTROGRAM_INT(spectrogram_hop, hop_ms)
SPECTROGRAM_FLOAT(spectrogram_f_min, f_min)
SPECTROGRAM_FLOAT(spectrogram_f_max, f_max)
SPECTROGRAM_FLOAT(spectrogram_max_db, max_db)
SPECTROGRAM_FLOAT(spectrogram_range_db, range_db)

static void set_spectrogram_resolution(void *ctx, int32_t val) { spectrogram_config_t c = *spectrogram_config(ctx); c.resolution_semitones = (float)val / 100.0f; spectrogram_set_config(ctx, &c); }
static int32_t get_spectrogram_resolution(void *ctx) { return (int32_t)lroundf(spectrogram_config(ctx)->resolution_semitones * 100.0f); }
static void set_spectrogram_channel(void *ctx, int32_t val) { spectrogram_config_t c = *spectrogram_config(ctx); c.channel = channel_from_param(val); spectrogram_set_config(ctx, &c); }
static int32_t get_spectrogram_channel(void *ctx) { return channel_to_param(spectrogram_config(ctx)->channel); }
static void set_spectrogram_palette(void *ctx, uint32_t val) { spectrogram_config_t c = *spectrogram_config(ctx); c.palette = (palette_t)val; spectrogram_set_config(ctx, &c); }
static uint32_t get_spectrogram_palette(void *ctx) { return (uint32_t)spectrogram_config(ctx)->palette; }
static void set_spectrogram_grid(void *ctx, bool val) { spectrogram_config_t c = *spectrogram_config(ctx); c.grid = val; spectrogram_set_config(ctx, &c); }
static bool get_spectrogram_grid(void *ctx) { return spectrogram_config(ctx)->grid != 0; }

#endif /* PN_SCHEMA_ONLY */

// -- tree

pn_param_t *s9_params_build(s9_params_t *p, void *osc, void *spectrum, void *periodic, void *spectrogram) {
	p->osc_params[0] = pn_float("time_per_div", osc, PN_FN(set_time_per_div), PN_FN(get_time_per_div), 0.00001f, 1.0f, 0);
	p->osc_params[1] = pn_float("amp_per_div",  osc, PN_FN(set_amp_per_div),  PN_FN(get_amp_per_div),  0.001f, 10.0f, 0);
	p->osc_params[2] = pn_bool("trig_auto",     osc, PN_FN(set_trig_auto),    PN_FN(get_trig_auto));
	p->osc_params[3] = pn_int("trig_slope",     osc, PN_FN(set_trig_slope),   PN_FN(get_trig_slope), -1, 1, 2);
	p->osc_params[4] = pn_float("trig_thresh",  osc, PN_FN(set_trig_thresh),  PN_FN(get_trig_thresh), -1.0f, 1.0f, 0);
	p->osc_params[5] = pn_float("trig_holdoff", osc, PN_FN(set_trig_holdoff), PN_FN(get_trig_holdoff), 0.0f, 1.0f, 0);
	p->osc_params[6] = pn_int("channel",        osc, PN_FN(set_channel),      PN_FN(get_channel),      -1, 31, 1);

	pn_param_t *spectrum_p = p->spectrum_params;
	spectrum_p[0] = pn_int("resolution", spectrum, PN_FN(set_spectrum_resolution), PN_FN(get_spectrum_resolution), 25, 400, 1);
	spectrum_p[1] = pn_int("padding",    spectrum, PN_FN(set_spectrum_padding),    PN_FN(get_spectrum_padding),    0, 3, 1);
	spectrum_p[2] = pn_int("f_min",      spectrum, PN_FN(set_spectrum_f_min),      PN_FN(get_spectrum_f_min),      20, 1000, 1);
	spectrum_p[3] = pn_int("f_max",      spectrum, PN_FN(set_spectrum_f_max),      PN_FN(get_spectrum_f_max),      1100, 24000, 1);
	spectrum_p[4] = pn_int("min_db",     spectrum, PN_FN(set_spectrum_min_db),     PN_FN(get_spectrum_min_db),     -140, 0, 1);
	spectrum_p[5] = pn_int("max_db",     spectrum, PN_FN(set_spectrum_max_db),     PN_FN(get_spectrum_max_db),     -100, 40, 1);

	pn_param_t *fundamental = p->periodic_fundamental_params;
	fundamental[S9_FUNDAMENTAL_FREQUENCY] = pn_readonly(pn_float("frequency", periodic, NULL, PN_FN(get_fundamental_frequency), 0.0f, 24000.0f, 0));
	fundamental[S9_FUNDAMENTAL_VALID]     = pn_readonly(pn_bool("valid",      periodic, NULL, PN_FN(get_fundamental_valid)));
	fundamental[S9_FUNDAMENTAL_MIDI]      = pn_readonly(pn_float("midi",      periodic, NULL, PN_FN(get_fundamental_midi), 0.0f, 160.0f, 0));
	fundamental[3]  = pn_float("window",          periodic, PN_FN(set_fundamental_window),           PN_FN(get_fundamental_window),           0.05f, 2.0f, 0);
	fundamental[4]  = pn_enum("window_function",  periodic, PN_FN(set_fundamental_window_function),  PN_FN(get_fundamental_window_function),  window_function_names, WINDOW_FUNCTION_COUNT);
	fundamental[5]  = pn_float("averaging",       periodic, PN_FN(set_fundamental_averaging),        PN_FN(get_fundamental_averaging),        0.0f, 10.0f, 0);
	fundamental[6]  = pn_float("f_min",           periodic, PN_FN(set_fundamental_f_min),            PN_FN(get_fundamental_f_min),            10.0f, 1000.0f, 0);
	fundamental[7]  = pn_float("f_max",           periodic, PN_FN(set_fundamental_f_max),            PN_FN(get_fundamental_f_max),            100.0f, 10000.0f, 0);
	fundamental[8]  = pn_float("gate_rms",        periodic, PN_FN(set_fundamental_gate_rms),         PN_FN(get_fundamental_gate_rms),         0.0f, 1.0f, 0);
	fundamental[9]  = pn_float("harmonicity_on", periodic, PN_FN(set_fundamental_harmonicity_on), PN_FN(get_fundamental_harmonicity_on),       0.0f, 1.0f, 0);
	fundamental[10]  = pn_float("harmonicity_off", periodic, PN_FN(set_fundamental_harmonicity_off), PN_FN(get_fundamental_harmonicity_off),      0.0f, 1.0f, 0);
	fundamental[11] = pn_float("hold",            periodic, PN_FN(set_fundamental_hold),             PN_FN(get_fundamental_hold),             0.0f, 2.0f, 0);
	fundamental[12] = pn_float("octave_tolerance", periodic, PN_FN(set_fundamental_octave_tolerance), PN_FN(get_fundamental_octave_tolerance), 0.0f, 200.0f, 0);
	fundamental[13] = pn_float("smoothing",       periodic, PN_FN(set_fundamental_smoothing),        PN_FN(get_fundamental_smoothing),        0.0f, 5.0f, 0);
	fundamental[14] = pn_float("deadband",        periodic, PN_FN(set_fundamental_deadband),         PN_FN(get_fundamental_deadband),         0.0f, 100.0f, 0);
	fundamental[15] = pn_float("snap",            periodic, PN_FN(set_fundamental_snap),             PN_FN(get_fundamental_snap),             0.0f, 1200.0f, 0);

	pn_param_t *harm = p->periodic_harmonics_params;
	harm[0] = pn_int("harmonics",        periodic, PN_FN(set_harm_harmonics),    PN_FN(get_harm_harmonics),    1, HARMONIC_MAX, 1);
	harm[1] = pn_float("prominence",     periodic, PN_FN(set_harm_prominence),   PN_FN(get_harm_prominence),   0.0f, 60.0f, 0);
	harm[2] = pn_float("hysteresis",     periodic, PN_FN(set_harm_hysteresis),   PN_FN(get_harm_hysteresis),   0.0f, 30.0f, 0);
	harm[3] = pn_float("readout_hold",   periodic, PN_FN(set_harm_readout_hold), PN_FN(get_harm_readout_hold), 0.0f, 5.0f, 0);

	pn_param_t *wave = p->periodic_waveform_params;
	wave[0] = pn_int("cycles",         periodic, PN_FN(set_waveform_cycles),      PN_FN(get_waveform_cycles),      1, 8, 1);
	wave[1] = pn_float("amp_per_div",  periodic, PN_FN(set_waveform_amp_per_div), PN_FN(get_waveform_amp_per_div), 0.001f, 10.0f, 0);
	wave[2] = pn_int("lock_cycles",    periodic, PN_FN(set_waveform_lock_cycles), PN_FN(get_waveform_lock_cycles), 1, 16, 1);
	wave[3] = pn_float("lock_time",    periodic, PN_FN(set_waveform_lock_time),   PN_FN(get_waveform_lock_time),   0.0f, 5.0f, 0);

	pn_param_t *note = p->periodic_note_params;
	note[0] = pn_float("a4", periodic, PN_FN(set_note_a4), PN_FN(get_note_a4), 400.0f, 480.0f, 0);

	p->periodic_params[0] = pn_int("channel", periodic, PN_FN(set_periodic_channel), PN_FN(get_periodic_channel), -1, 31, 1);
	p->periodic_params[1] = pn_enum("view",   periodic, PN_FN(set_periodic_view),    PN_FN(get_periodic_view),    periodic_view_names, PERIODIC_VIEW_COUNT);
	p->periodic_params[2] = pn_group("fundamental", fundamental, S9_PERIODIC_FUNDAMENTAL_NUM_PARAMS);
	p->periodic_params[3] = pn_group("harmonics", harm, S9_PERIODIC_HARMONICS_NUM_PARAMS);
	p->periodic_params[4] = pn_group("waveform", wave, S9_PERIODIC_WAVEFORM_NUM_PARAMS);
	p->periodic_params[5] = pn_group("note", note, S9_PERIODIC_NOTE_NUM_PARAMS);

	pn_param_t *spec = p->spectrogram_params;
	spec[S9_SPECTROGRAM_TIME_RESOLUTION] = pn_readonly(pn_float("time_resolution", spectrogram, NULL, PN_FN(get_spectrogram_time_resolution), 0.0f, 60.0f, 0));
	spec[S9_SPECTROGRAM_FLOOR_FREQUENCY] = pn_readonly(pn_float("floor_frequency", spectrogram, NULL, PN_FN(get_spectrogram_floor_frequency), 0.0f, 24000.0f, 0));
	spec[2] = pn_int("channel",           spectrogram, PN_FN(set_spectrogram_channel),         PN_FN(get_spectrogram_channel),         -1, 31, 1);
	spec[3] = pn_int("hop",               spectrogram, PN_FN(set_spectrogram_hop),             PN_FN(get_spectrogram_hop),             5, 60, 1);
	spec[4] = pn_int("resolution",        spectrogram, PN_FN(set_spectrogram_resolution),      PN_FN(get_spectrogram_resolution),      25, 400, 1);
	spec[5] = pn_float("f_min",           spectrogram, PN_FN(set_spectrogram_f_min),           PN_FN(get_spectrogram_f_min),           20.0f, 1000.0f, 0);
	spec[6] = pn_float("f_max",           spectrogram, PN_FN(set_spectrogram_f_max),           PN_FN(get_spectrogram_f_max),           1100.0f, 24000.0f, 0);
	spec[7] = pn_float("max_db",          spectrogram, PN_FN(set_spectrogram_max_db),          PN_FN(get_spectrogram_max_db),          -60.0f, 0.0f, 0);
	spec[8] = pn_float("range_db",        spectrogram, PN_FN(set_spectrogram_range_db),        PN_FN(get_spectrogram_range_db),        10.0f, 120.0f, 0);
	spec[9] = pn_enum("palette",          spectrogram, PN_FN(set_spectrogram_palette),         PN_FN(get_spectrogram_palette),         palette_names, PALETTE_COUNT);
	spec[10] = pn_bool("grid",            spectrogram, PN_FN(set_spectrogram_grid),            PN_FN(get_spectrogram_grid));

	p->debug_params[0] = pn_bool("fps", NULL, PN_FN(set_fps), PN_FN(get_fps));

	p->root_children[S9_ROOT_MODE]        = pn_enum("mode", NULL, PN_FN(set_mode), PN_FN(get_mode), s9_mode_names, S9_MODE_COUNT);
	p->root_children[S9_ROOT_OSC]         = pn_group("osc", p->osc_params, S9_OSC_NUM_PARAMS);
	p->root_children[S9_ROOT_SPECTRUM]    = pn_group("spectrum", p->spectrum_params, S9_SPECTRUM_NUM_PARAMS);
	p->root_children[S9_ROOT_PERIODIC]    = pn_group("periodic", p->periodic_params, S9_PERIODIC_NUM_PARAMS);
	p->root_children[S9_ROOT_SPECTROGRAM] = pn_group("spectrogram", p->spectrogram_params, S9_SPECTROGRAM_NUM_PARAMS);
	p->root_children[S9_ROOT_DEBUG]       = pn_group("debug", p->debug_params, S9_DEBUG_NUM_PARAMS);
	p->root = pn_group("s9", p->root_children, S9_ROOT_COUNT);
	return &p->root;
}
