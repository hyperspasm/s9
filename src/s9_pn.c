#include "s9_pn.h"
#include "s9_params.h"
#include "s9.h"

#include <pn/pn.h>
#include <pn_uds.h>

#include <stdlib.h>

#define SOCKET_PATH    "/tmp/s9.sock"
#define UDS_READ_BUF   1024

struct s9_pn {
	s9_pn_config_t config;
	s9_params_t params;
	pn_server_t *server;
	pn_uds_t *uds;
};

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
	void *osc = s9_components_get(pn->config.components, COMP_OSCILLOSCOPE)->instance;
	pn_param_t *root = s9_params_build(&pn->params, osc);

	// allocate server
	pn_server_config_t server_config = {
		.root = root,
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
	if (pn_server_start(pn->server) != 0) {
		const pn_param_t *offender = NULL;
		pn_schema_result_t result = pn_schema_validate(root, &offender);
		S9_LOG_ERROR("pn: parameter tree rejected: %s (%s)",
					 pn_schema_result_name(result),
					 offender && offender->node.name ? offender->node.name : "-");
		free(pn->uds);
		free(pn->server);
		free(pn);
		return NULL;
	}
	S9_LOG_INFO("pn: schema hash %08x", (unsigned)pn_server_schema_hash(pn->server));

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

void s9_pn_notify_mode(s9_pn_t *pn) {
	if (pn) pn_param_notify(&pn->params.root_children[S9_ROOT_MODE]);
}
