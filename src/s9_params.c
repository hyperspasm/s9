#include "s9_params.h"
#include "s9_modes.h"
#include "window_function.h"
#include "harmonic_readout.h"

#ifndef PN_SCHEMA_ONLY
#include "s9_app.h"
#include "oscilloscope.h"
#include "harmonics.h"
#include "pitch_tracker.h"

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

// -- harmonics callbacks: each parameter is one field of the component's configuration

#define HARM_FLOAT(param, field) \
	static void set_##param(void *ctx, float val) { harmonics_config_t c = *harmonics_config(ctx); c.field = val; harmonics_set_config(ctx, &c); } \
	static float get_##param(void *ctx) { return harmonics_config(ctx)->field; }

#define HARM_INT(param, field) \
	static void set_##param(void *ctx, int32_t val) { harmonics_config_t c = *harmonics_config(ctx); c.field = (uint32_t)val; harmonics_set_config(ctx, &c); } \
	static int32_t get_##param(void *ctx) { return (int32_t)harmonics_config(ctx)->field; }

HARM_INT(harm_harmonics, reader.harmonics)
HARM_FLOAT(harm_resolution, resolution_hz)
HARM_FLOAT(harm_prominence, reader.prominence_db)
HARM_FLOAT(harm_averaging, averaging_s)
HARM_FLOAT(harm_hysteresis, reader.hysteresis_db)
HARM_FLOAT(harm_readout_hold, reader.hold_s)

static void set_harm_channel(void *ctx, int32_t val) { harmonics_config_t c = *harmonics_config(ctx); c.channel = channel_from_param(val); harmonics_set_config(ctx, &c); }
static int32_t get_harm_channel(void *ctx) { return channel_to_param(harmonics_config(ctx)->channel); }

static void set_harm_window(void *ctx, uint32_t val) { harmonics_config_t c = *harmonics_config(ctx); c.window_function = (window_function_t)val; harmonics_set_config(ctx, &c); }
static uint32_t get_harm_window(void *ctx) { return (uint32_t)harmonics_config(ctx)->window_function; }

// -- pitch tracker callbacks: what it tracks, read only, and each field of its configuration

static float get_pitch_frequency(void *ctx) { return pitch_tracker_frequency(ctx); }
static bool get_pitch_valid(void *ctx) { return pitch_tracker_valid(ctx); }

#define PITCH_FLOAT(param, field) \
	static void set_##param(void *ctx, float val) { pitch_tracker_config_t c = *pitch_tracker_config(ctx); c.field = val; pitch_tracker_set_config(ctx, &c); } \
	static float get_##param(void *ctx) { return pitch_tracker_config(ctx)->field; }

PITCH_FLOAT(pitch_resolution, resolution_hz)
PITCH_FLOAT(pitch_f_min, f_min)
PITCH_FLOAT(pitch_f_max, f_max)
PITCH_FLOAT(pitch_gate_rms, conditioner.gate_rms)
PITCH_FLOAT(pitch_clarity_on, conditioner.clarity_on)
PITCH_FLOAT(pitch_clarity_off, conditioner.clarity_off)
PITCH_FLOAT(pitch_hold, conditioner.hold_s)
PITCH_FLOAT(pitch_octave_tolerance, conditioner.octave_tolerance_cents)
PITCH_FLOAT(pitch_smoothing, conditioner.smoothing)
PITCH_FLOAT(pitch_deadband, conditioner.deadband_cents)

static void set_pitch_channel(void *ctx, int32_t val) { pitch_tracker_config_t c = *pitch_tracker_config(ctx); c.channel = channel_from_param(val); pitch_tracker_set_config(ctx, &c); }
static int32_t get_pitch_channel(void *ctx) { return channel_to_param(pitch_tracker_config(ctx)->channel); }
static void set_pitch_window(void *ctx, uint32_t val) { pitch_tracker_config_t c = *pitch_tracker_config(ctx); c.window_function = (window_function_t)val; pitch_tracker_set_config(ctx, &c); }
static uint32_t get_pitch_window(void *ctx) { return (uint32_t)pitch_tracker_config(ctx)->window_function; }

#endif /* PN_SCHEMA_ONLY */

// -- tree

pn_param_t *s9_params_build(s9_params_t *p, void *osc, void *harm, void *tracker) {
	p->osc_params[0] = pn_float("time_per_div", osc, PN_FN(set_time_per_div), PN_FN(get_time_per_div), 0.00001f, 1.0f, 0);
	p->osc_params[1] = pn_float("amp_per_div",  osc, PN_FN(set_amp_per_div),  PN_FN(get_amp_per_div),  0.001f, 10.0f, 0);
	p->osc_params[2] = pn_bool("trig_auto",     osc, PN_FN(set_trig_auto),    PN_FN(get_trig_auto));
	p->osc_params[3] = pn_int("trig_slope",     osc, PN_FN(set_trig_slope),   PN_FN(get_trig_slope), -1, 1, 2);
	p->osc_params[4] = pn_float("trig_thresh",  osc, PN_FN(set_trig_thresh),  PN_FN(get_trig_thresh), -1.0f, 1.0f, 0);
	p->osc_params[5] = pn_float("trig_holdoff", osc, PN_FN(set_trig_holdoff), PN_FN(get_trig_holdoff), 0.0f, 1.0f, 0);
	p->osc_params[6] = pn_int("channel",        osc, PN_FN(set_channel),      PN_FN(get_channel),      -1, 31, 1);

	p->harm_params[0]  = pn_int("channel",           harm, PN_FN(set_harm_channel),          PN_FN(get_harm_channel),          -1, 31, 1);
	p->harm_params[1]  = pn_int("harmonics",         harm, PN_FN(set_harm_harmonics),        PN_FN(get_harm_harmonics),        1, HARMONIC_MAX, 1);
	p->harm_params[2]  = pn_float("resolution",      harm, PN_FN(set_harm_resolution),       PN_FN(get_harm_resolution),       1.0f, 50.0f, 0);
	p->harm_params[3]  = pn_enum("window",           harm, PN_FN(set_harm_window),           PN_FN(get_harm_window),           window_function_names, WINDOW_FUNCTION_COUNT);
	p->harm_params[4]  = pn_float("prominence",      harm, PN_FN(set_harm_prominence),       PN_FN(get_harm_prominence),       0.0f, 60.0f, 0);
	p->harm_params[5]  = pn_float("averaging",       harm, PN_FN(set_harm_averaging),        PN_FN(get_harm_averaging),        0.0f, 10.0f, 0);
	p->harm_params[6]  = pn_float("hysteresis",      harm, PN_FN(set_harm_hysteresis),       PN_FN(get_harm_hysteresis),       0.0f, 30.0f, 0);
	p->harm_params[7]  = pn_float("readout_hold",    harm, PN_FN(set_harm_readout_hold),     PN_FN(get_harm_readout_hold),     0.0f, 5.0f, 0);

	p->pitch_params[S9_PITCH_FREQUENCY] = pn_float("frequency", tracker, NULL, PN_FN(get_pitch_frequency), 0.0f, 24000.0f, 0);
	p->pitch_params[S9_PITCH_VALID]     = pn_bool("valid",      tracker, NULL, PN_FN(get_pitch_valid));
	p->pitch_params[2]  = pn_int("channel",           tracker, PN_FN(set_pitch_channel),          PN_FN(get_pitch_channel),          -1, 31, 1);
	p->pitch_params[3]  = pn_float("resolution",      tracker, PN_FN(set_pitch_resolution),       PN_FN(get_pitch_resolution),       1.0f, 50.0f, 0);
	p->pitch_params[4]  = pn_enum("window",           tracker, PN_FN(set_pitch_window),           PN_FN(get_pitch_window),           window_function_names, WINDOW_FUNCTION_COUNT);
	p->pitch_params[5]  = pn_float("f_min",           tracker, PN_FN(set_pitch_f_min),            PN_FN(get_pitch_f_min),            10.0f, 1000.0f, 0);
	p->pitch_params[6]  = pn_float("f_max",           tracker, PN_FN(set_pitch_f_max),            PN_FN(get_pitch_f_max),            100.0f, 10000.0f, 0);
	p->pitch_params[7]  = pn_float("gate_rms",        tracker, PN_FN(set_pitch_gate_rms),         PN_FN(get_pitch_gate_rms),         0.0f, 1.0f, 0);
	p->pitch_params[8]  = pn_float("clarity_on",      tracker, PN_FN(set_pitch_clarity_on),       PN_FN(get_pitch_clarity_on),       0.0f, 1.0f, 0);
	p->pitch_params[9]  = pn_float("clarity_off",     tracker, PN_FN(set_pitch_clarity_off),      PN_FN(get_pitch_clarity_off),      0.0f, 1.0f, 0);
	p->pitch_params[10] = pn_float("hold",            tracker, PN_FN(set_pitch_hold),             PN_FN(get_pitch_hold),             0.0f, 2.0f, 0);
	p->pitch_params[11] = pn_float("octave_tolerance", tracker, PN_FN(set_pitch_octave_tolerance), PN_FN(get_pitch_octave_tolerance), 0.0f, 200.0f, 0);
	p->pitch_params[12] = pn_float("smoothing",       tracker, PN_FN(set_pitch_smoothing),        PN_FN(get_pitch_smoothing),        0.0f, 1.0f, 0);
	p->pitch_params[13] = pn_float("deadband",        tracker, PN_FN(set_pitch_deadband),         PN_FN(get_pitch_deadband),         0.0f, 100.0f, 0);

	p->debug_params[0] = pn_bool("fps", NULL, PN_FN(set_fps), PN_FN(get_fps));

	p->root_children[S9_ROOT_MODE]  = pn_enum("mode", NULL, PN_FN(set_mode), PN_FN(get_mode), s9_mode_names, S9_MODE_COUNT);
	p->root_children[S9_ROOT_OSC]   = pn_group("osc", p->osc_params, S9_OSC_NUM_PARAMS);
	p->root_children[S9_ROOT_HARM]  = pn_group("harm", p->harm_params, S9_HARM_NUM_PARAMS);
	p->root_children[S9_ROOT_PITCH] = pn_group("pitch", p->pitch_params, S9_PITCH_NUM_PARAMS);
	p->root_children[S9_ROOT_DEBUG] = pn_group("debug", p->debug_params, S9_DEBUG_NUM_PARAMS);
	p->root = pn_group("s9", p->root_children, S9_ROOT_COUNT);
	return &p->root;
}
