#include "s9_pn.h"
#include "s9.h"
#include "oscilloscope.h"

#include <pn/pn.h>
#include <pn_uds.h>

#include <stdlib.h>

#define SOCKET_PATH    "/tmp/s9.sock"
#define UDS_READ_BUF   1024

#define OSC_NUM_PARAMS 7

struct s9_pn {
	s9_pn_config_t config;

	pn_param_t osc_params[OSC_NUM_PARAMS];
	pn_param_t osc_group;
	pn_param_t root;

	pn_server_t *server;
	pn_uds_t *uds;
};

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

// -- tree

static void build_tree(s9_pn_t *pn) {
	oscilloscope_t *osc = s9_components_get(pn->config.components, COMP_OSCILLOSCOPE)->instance;

	pn->osc_params[0] = pn_float("time_per_div", osc, set_time_per_div, get_time_per_div, 0.00001f, 1.0f, 0);
	pn->osc_params[1] = pn_float("amp_per_div",  osc, set_amp_per_div,  get_amp_per_div,  0.001f, 10.0f, 0);
	pn->osc_params[2] = pn_bool("trig_auto",     osc, set_trig_auto,    get_trig_auto);
	pn->osc_params[3] = pn_int("trig_slope",     osc, set_trig_slope,   get_trig_slope, -1, 1, 2);
	pn->osc_params[4] = pn_float("trig_thresh",  osc, set_trig_thresh,  get_trig_thresh, -1.0f, 1.0f, 0);
	pn->osc_params[5] = pn_float("trig_holdoff", osc, set_trig_holdoff, get_trig_holdoff, 0.0f, 1.0f, 0);
	pn->osc_params[6] = pn_int("channel",       osc, set_channel,      get_channel,      -1, 31, 1);

	pn->osc_group = pn_group("osc", pn->osc_params, OSC_NUM_PARAMS);
	pn->root = pn_group("s9", &pn->osc_group, 1);
}

// -- transport

static void on_recv(const uint8_t *data, uint32_t len, void *ctx) {
	pn_server_feed((pn_server_t *)ctx, data, len);
}

// -- public

s9_pn_t *s9_pn_new(const s9_pn_config_t *config) {
	s9_pn_t *pn = malloc(sizeof(s9_pn_t));
	CHECK_ALLOC(pn, return NULL);
	memset(pn, 0, sizeof(*pn));

	pn->config = *config;
	build_tree(pn);

	// allocate server
	pn_server_config_t server_config = {
		.root = &pn->root,
		.send = pn_uds_send,
	};
	uint32_t server_size = pn_server_alloc_size(&server_config);
	pn->server = malloc(server_size);
	CHECK_ALLOC(pn->server, free(pn); return NULL);

	// allocate and init UDS transport
	pn_uds_config_t uds_config = {
		.path = SOCKET_PATH,
		.read_buf_size = UDS_READ_BUF,
		.on_recv = on_recv,
		.on_recv_ctx = pn->server,
	};
	uint32_t uds_size = pn_uds_alloc_size(&uds_config);
	pn->uds = malloc(uds_size);
	CHECK_ALLOC(pn->uds, free(pn->server); free(pn); return NULL);
	pn_uds_init(pn->uds, &uds_config);

	// init and start server
	server_config.send_ctx = pn->uds;
	pn_server_init(pn->server, &server_config);
	pn_server_start(pn->server);

	// listen
	if (pn_uds_listen(pn->uds) != 0) {
		S9_LOG_WARNING("pn: failed to listen on %s", SOCKET_PATH);
	} else {
		S9_LOG_INFO("pn: listening on %s", SOCKET_PATH);
	}

	return pn;
}

void s9_pn_free(s9_pn_t **pn) {
	if (pn && *pn) {
		if ((*pn)->uds) pn_uds_close((*pn)->uds);
		free((*pn)->uds);
		free((*pn)->server);
		free(*pn);
		*pn = NULL;
	}
}

void s9_pn_update(s9_pn_t *pn) {
	if (pn && pn->uds) pn_uds_poll(pn->uds);
}
