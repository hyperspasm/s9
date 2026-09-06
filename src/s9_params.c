#include "s9_params.h"
#include "s9_modes.h"

#ifndef PN_SCHEMA_ONLY
#include "s9_app.h"
#include "oscilloscope.h"

// -- mode callbacks

static void set_mode(void *ctx, uint32_t val) { (void)ctx; s9_app_set_mode((s9_mode_id_t)val); }
static uint32_t get_mode(void *ctx) { (void)ctx; return (uint32_t)s9_app_get_mode(); }

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

static void set_channel(void *ctx, int32_t val) { oscilloscope_set_channel(ctx, (uint32_t)val); }
static int32_t get_channel(void *ctx) { return (int32_t)oscilloscope_get_channel(ctx); }

#endif /* PN_SCHEMA_ONLY */

// -- tree

pn_param_t *s9_params_build(s9_params_t *p, void *osc) {
	p->osc_params[0] = pn_float("time_per_div", osc, PN_FN(set_time_per_div), PN_FN(get_time_per_div), 0.00001f, 1.0f, 0);
	p->osc_params[1] = pn_float("amp_per_div",  osc, PN_FN(set_amp_per_div),  PN_FN(get_amp_per_div),  0.001f, 10.0f, 0);
	p->osc_params[2] = pn_bool("trig_auto",     osc, PN_FN(set_trig_auto),    PN_FN(get_trig_auto));
	p->osc_params[3] = pn_int("trig_slope",     osc, PN_FN(set_trig_slope),   PN_FN(get_trig_slope), -1, 1, 2);
	p->osc_params[4] = pn_float("trig_thresh",  osc, PN_FN(set_trig_thresh),  PN_FN(get_trig_thresh), -1.0f, 1.0f, 0);
	p->osc_params[5] = pn_float("trig_holdoff", osc, PN_FN(set_trig_holdoff), PN_FN(get_trig_holdoff), 0.0f, 1.0f, 0);
	p->osc_params[6] = pn_int("channel",        osc, PN_FN(set_channel),      PN_FN(get_channel),      -1, 31, 1);

	p->root_children[S9_ROOT_MODE] = pn_enum("mode", NULL, PN_FN(set_mode), PN_FN(get_mode), s9_mode_names, S9_MODE_COUNT);
	p->root_children[S9_ROOT_OSC]  = pn_group("osc", p->osc_params, S9_OSC_NUM_PARAMS);
	p->root = pn_group("s9", p->root_children, S9_ROOT_COUNT);
	return &p->root;
}
