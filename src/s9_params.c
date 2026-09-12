#include "s9_params.h"
#include "s9_modes.h"
#include "window_function.h"
#include "harmonic_readout.h"
#ifdef PN_SCHEMA_ONLY
// the view names, without the component behind them
typedef enum { PERIODIC_VIEW_HARMONICS, PERIODIC_VIEW_COUNT } periodic_view_t;
static const char *const periodic_view_names[PERIODIC_VIEW_COUNT] = { [PERIODIC_VIEW_HARMONICS] = "harmonics" };
#endif

#ifndef PN_SCHEMA_ONLY
#include "s9_app.h"
#include "oscilloscope.h"
#include "periodic.h"

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

// -- periodic analyzer callbacks: what it tracks, read only, and each field of its configuration

static float get_pitch_frequency(void *ctx) { return periodic_frequency(ctx); }
static bool get_pitch_valid(void *ctx) { return periodic_valid(ctx); }

#define PERIODIC_FLOAT(param, field) \
	static void set_##param(void *ctx, float val) { periodic_config_t c = *periodic_config(ctx); c.field = val; periodic_set_config(ctx, &c); } \
	static float get_##param(void *ctx) { return periodic_config(ctx)->field; }

#define PERIODIC_INT(param, field) \
	static void set_##param(void *ctx, int32_t val) { periodic_config_t c = *periodic_config(ctx); c.field = (uint32_t)val; periodic_set_config(ctx, &c); } \
	static int32_t get_##param(void *ctx) { return (int32_t)periodic_config(ctx)->field; }

PERIODIC_FLOAT(pitch_window, tracker.window_s)
PERIODIC_FLOAT(pitch_averaging, tracker.averaging_s)
PERIODIC_FLOAT(pitch_f_min, tracker.f_min)
PERIODIC_FLOAT(pitch_f_max, tracker.f_max)
PERIODIC_FLOAT(pitch_gate_rms, tracker.conditioner.gate_rms)
PERIODIC_FLOAT(pitch_clarity_on, tracker.conditioner.clarity_on)
PERIODIC_FLOAT(pitch_clarity_off, tracker.conditioner.clarity_off)
PERIODIC_FLOAT(pitch_hold, tracker.conditioner.hold_s)
PERIODIC_FLOAT(pitch_octave_tolerance, tracker.conditioner.octave_tolerance_cents)
PERIODIC_FLOAT(pitch_smoothing, tracker.conditioner.smoothing_s)
PERIODIC_FLOAT(pitch_deadband, tracker.conditioner.deadband_cents)
PERIODIC_INT(harm_harmonics, harmonics.reader.harmonics)
PERIODIC_FLOAT(harm_prominence, harmonics.reader.prominence_db)
PERIODIC_FLOAT(harm_hysteresis, harmonics.reader.hysteresis_db)
PERIODIC_FLOAT(harm_readout_hold, harmonics.reader.hold_s)

static void set_periodic_channel(void *ctx, int32_t val) { periodic_config_t c = *periodic_config(ctx); c.channel = channel_from_param(val); periodic_set_config(ctx, &c); }
static int32_t get_periodic_channel(void *ctx) { return channel_to_param(periodic_config(ctx)->channel); }
static void set_periodic_view(void *ctx, uint32_t val) { periodic_config_t c = *periodic_config(ctx); c.view = (periodic_view_t)val; periodic_set_config(ctx, &c); }
static uint32_t get_periodic_view(void *ctx) { return (uint32_t)periodic_config(ctx)->view; }
static void set_pitch_window_function(void *ctx, uint32_t val) { periodic_config_t c = *periodic_config(ctx); c.tracker.window_function = (window_function_t)val; periodic_set_config(ctx, &c); }
static uint32_t get_pitch_window_function(void *ctx) { return (uint32_t)periodic_config(ctx)->tracker.window_function; }

#endif /* PN_SCHEMA_ONLY */

// -- tree

pn_param_t *s9_params_build(s9_params_t *p, void *osc, void *periodic) {
	p->osc_params[0] = pn_float("time_per_div", osc, PN_FN(set_time_per_div), PN_FN(get_time_per_div), 0.00001f, 1.0f, 0);
	p->osc_params[1] = pn_float("amp_per_div",  osc, PN_FN(set_amp_per_div),  PN_FN(get_amp_per_div),  0.001f, 10.0f, 0);
	p->osc_params[2] = pn_bool("trig_auto",     osc, PN_FN(set_trig_auto),    PN_FN(get_trig_auto));
	p->osc_params[3] = pn_int("trig_slope",     osc, PN_FN(set_trig_slope),   PN_FN(get_trig_slope), -1, 1, 2);
	p->osc_params[4] = pn_float("trig_thresh",  osc, PN_FN(set_trig_thresh),  PN_FN(get_trig_thresh), -1.0f, 1.0f, 0);
	p->osc_params[5] = pn_float("trig_holdoff", osc, PN_FN(set_trig_holdoff), PN_FN(get_trig_holdoff), 0.0f, 1.0f, 0);
	p->osc_params[6] = pn_int("channel",        osc, PN_FN(set_channel),      PN_FN(get_channel),      -1, 31, 1);

	pn_param_t *pitch = p->periodic_pitch_params;
	pitch[S9_PITCH_FREQUENCY] = pn_float("frequency", periodic, NULL, PN_FN(get_pitch_frequency), 0.0f, 24000.0f, 0);
	pitch[S9_PITCH_VALID]     = pn_bool("valid",      periodic, NULL, PN_FN(get_pitch_valid));
	pitch[2]  = pn_float("window",          periodic, PN_FN(set_pitch_window),           PN_FN(get_pitch_window),           0.05f, 2.0f, 0);
	pitch[3]  = pn_enum("window_function",  periodic, PN_FN(set_pitch_window_function),  PN_FN(get_pitch_window_function),  window_function_names, WINDOW_FUNCTION_COUNT);
	pitch[4]  = pn_float("averaging",       periodic, PN_FN(set_pitch_averaging),        PN_FN(get_pitch_averaging),        0.0f, 10.0f, 0);
	pitch[5]  = pn_float("f_min",           periodic, PN_FN(set_pitch_f_min),            PN_FN(get_pitch_f_min),            10.0f, 1000.0f, 0);
	pitch[6]  = pn_float("f_max",           periodic, PN_FN(set_pitch_f_max),            PN_FN(get_pitch_f_max),            100.0f, 10000.0f, 0);
	pitch[7]  = pn_float("gate_rms",        periodic, PN_FN(set_pitch_gate_rms),         PN_FN(get_pitch_gate_rms),         0.0f, 1.0f, 0);
	pitch[8]  = pn_float("clarity_on",      periodic, PN_FN(set_pitch_clarity_on),       PN_FN(get_pitch_clarity_on),       0.0f, 1.0f, 0);
	pitch[9]  = pn_float("clarity_off",     periodic, PN_FN(set_pitch_clarity_off),      PN_FN(get_pitch_clarity_off),      0.0f, 1.0f, 0);
	pitch[10] = pn_float("hold",            periodic, PN_FN(set_pitch_hold),             PN_FN(get_pitch_hold),             0.0f, 2.0f, 0);
	pitch[11] = pn_float("octave_tolerance", periodic, PN_FN(set_pitch_octave_tolerance), PN_FN(get_pitch_octave_tolerance), 0.0f, 200.0f, 0);
	pitch[12] = pn_float("smoothing",       periodic, PN_FN(set_pitch_smoothing),        PN_FN(get_pitch_smoothing),        0.0f, 5.0f, 0);
	pitch[13] = pn_float("deadband",        periodic, PN_FN(set_pitch_deadband),         PN_FN(get_pitch_deadband),         0.0f, 100.0f, 0);

	pn_param_t *harm = p->periodic_harmonics_params;
	harm[0] = pn_int("harmonics",        periodic, PN_FN(set_harm_harmonics),    PN_FN(get_harm_harmonics),    1, HARMONIC_MAX, 1);
	harm[1] = pn_float("prominence",     periodic, PN_FN(set_harm_prominence),   PN_FN(get_harm_prominence),   0.0f, 60.0f, 0);
	harm[2] = pn_float("hysteresis",     periodic, PN_FN(set_harm_hysteresis),   PN_FN(get_harm_hysteresis),   0.0f, 30.0f, 0);
	harm[3] = pn_float("readout_hold",   periodic, PN_FN(set_harm_readout_hold), PN_FN(get_harm_readout_hold), 0.0f, 5.0f, 0);

	p->periodic_params[0] = pn_int("channel", periodic, PN_FN(set_periodic_channel), PN_FN(get_periodic_channel), -1, 31, 1);
	p->periodic_params[1] = pn_enum("view",   periodic, PN_FN(set_periodic_view),    PN_FN(get_periodic_view),    periodic_view_names, PERIODIC_VIEW_COUNT);
	p->periodic_params[2] = pn_group("pitch", pitch, S9_PERIODIC_PITCH_NUM_PARAMS);
	p->periodic_params[3] = pn_group("harmonics", harm, S9_PERIODIC_HARMONICS_NUM_PARAMS);

	p->debug_params[0] = pn_bool("fps", NULL, PN_FN(set_fps), PN_FN(get_fps));

	p->root_children[S9_ROOT_MODE]     = pn_enum("mode", NULL, PN_FN(set_mode), PN_FN(get_mode), s9_mode_names, S9_MODE_COUNT);
	p->root_children[S9_ROOT_OSC]      = pn_group("osc", p->osc_params, S9_OSC_NUM_PARAMS);
	p->root_children[S9_ROOT_PERIODIC] = pn_group("periodic", p->periodic_params, S9_PERIODIC_NUM_PARAMS);
	p->root_children[S9_ROOT_DEBUG]    = pn_group("debug", p->debug_params, S9_DEBUG_NUM_PARAMS);
	p->root = pn_group("s9", p->root_children, S9_ROOT_COUNT);
	return &p->root;
}
