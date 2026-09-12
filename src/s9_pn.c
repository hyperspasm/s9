#include "s9_pn.h"
#include "s9_params.h"
#include "s9_ws.h"
#include "periodic.h"
#include "s9.h"

#include <pn/pn.h>

#include <stdlib.h>

struct s9_pn {
	s9_pn_config_t config;
	s9_params_t params;
	pn_server_t *server;
	s9_ws_t *ws;
	char *manifest;
	uint32_t manifest_len;

	// the pitch last pushed to clients
	periodic_t *periodic;
	bool pitch_valid;
	float pitch_frequency;
};

// -- manifest text

// Sink that counts bytes, then one that copies them: the manifest is written
// twice, once to size the buffer and once to fill it.
static int count_sink(const uint8_t *buf, uint32_t len, void *ctx) {
	(void)buf;
	*(uint32_t *)ctx += len;
	return 0;
}

typedef struct { char *buf; uint32_t pos; } copy_sink_t;

static int copy_sink(const uint8_t *buf, uint32_t len, void *ctx) {
	copy_sink_t *s = ctx;
	memcpy(s->buf + s->pos, buf, len);
	s->pos += len;
	return 0;
}

static char *manifest_text(const pn_param_t *root, uint32_t *len) {
	uint32_t size = 0;
	if (pn_manifest_write(root, count_sink, &size) != 0) return NULL;
	char *text = malloc(size);
	CHECK_ALLOC(text, return NULL);
	copy_sink_t s = { text, 0 };
	if (pn_manifest_write(root, copy_sink, &s) != 0) { free(text); return NULL; }
	*len = size;
	return text;
}

// -- transport

static void on_recv(const uint8_t *data, uint32_t len, void *ctx) {
	pn_server_feed((pn_server_t *)ctx, data, len);
}

// Everything the server sends goes to every websocket client. Nobody being
// connected is not a failure.
static int send_all(const uint8_t *data, uint32_t len, void *ctx) {
	s9_pn_t *pn = ctx;
	if (pn->ws) return s9_ws_send(data, len, pn->ws);
	return 0;
}

// -- public

s9_pn_t *s9_pn_new(const s9_pn_config_t *config) {
	s9_pn_t *pn = malloc(sizeof(s9_pn_t));
	CHECK_ALLOC(pn, return NULL);
	memset(pn, 0, sizeof(*pn));

	pn->config = *config;
	void *osc = s9_components_get(pn->config.components, COMP_OSCILLOSCOPE)->instance;
	pn->periodic = s9_components_get(pn->config.components, COMP_PERIODIC)->instance;
	pn_param_t *root = s9_params_build(&pn->params, osc, pn->periodic);

	// allocate server
	pn_server_config_t server_config = {
		.root = root,
		.send = send_all,
		.send_ctx = pn,
	};
	uint32_t server_size = pn_server_alloc_size(&server_config);
	pn->server = malloc(server_size);
	CHECK_ALLOC(pn->server, free(pn); return NULL);

	// init and start server
	pn_server_init(pn->server, &server_config);
	if (pn_server_start(pn->server) != 0) {
		const pn_param_t *offender = NULL;
		pn_schema_result_t result = pn_schema_validate(root, &offender);
		S9_LOG_ERROR("pn: parameter tree rejected: %s (%s)",
					 pn_schema_result_name(result),
					 offender && offender->node.name ? offender->node.name : "-");
		free(pn->server);
		free(pn);
		return NULL;
	}
	S9_LOG_INFO("pn: schema hash %08x", (unsigned)pn_server_schema_hash(pn->server));

	// the manifest, for serving to clients
	pn->manifest = manifest_text(root, &pn->manifest_len);
	CHECK_ALLOC(pn->manifest, free(pn->server); free(pn); return NULL);

	// websocket hub and web interface
	pn->ws = s9_ws_new(&(s9_ws_config_t){
		.port = pn->config.port,
		.www_dir = pn->config.www_dir,
		.manifest = pn->manifest,
		.manifest_len = pn->manifest_len,
		.on_recv = on_recv,
		.on_recv_ctx = pn->server,
	});
	if (pn->ws) {
		S9_LOG_INFO("pn: serving on port %u", (unsigned)pn->config.port);
	} else {
		S9_LOG_WARNING("pn: parameters and web interface unavailable");
	}

	return pn;
}

void s9_pn_free(s9_pn_t **pn) {
	if (pn && *pn) {
		s9_ws_free(&(*pn)->ws);
		free((*pn)->server);
		free((*pn)->manifest);
		free(*pn);
		*pn = NULL;
	}
}

// The tracked pitch changes on its own, so its parameters are pushed
// whenever their values move.
static void notify_pitch(s9_pn_t *pn) {
	bool valid = periodic_valid(pn->periodic);
	float frequency = periodic_frequency(pn->periodic);
	if (valid != pn->pitch_valid) {
		pn->pitch_valid = valid;
		pn_param_notify(&pn->params.periodic_pitch_params[S9_PITCH_VALID]);
	}
	if (frequency != pn->pitch_frequency) {
		pn->pitch_frequency = frequency;
		pn_param_notify(&pn->params.periodic_pitch_params[S9_PITCH_FREQUENCY]);
	}
}

void s9_pn_update(s9_pn_t *pn) {
	if (!pn) return;
	if (pn->ws) s9_ws_update(pn->ws);
	notify_pitch(pn);
}

void s9_pn_notify_mode(s9_pn_t *pn) {
	if (pn) pn_param_notify(&pn->params.root_children[S9_ROOT_MODE]);
}

const char *s9_pn_manifest(const s9_pn_t *pn, uint32_t *len) {
	*len = pn->manifest_len;
	return pn->manifest;
}
