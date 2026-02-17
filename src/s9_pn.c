#include "s9_pn.h"
#include "s9.h"
#include "oscilloscope.h"

#include <pn/pn.h>
#include <pn_uds.h>

#include <stdlib.h>

#define SOCKET_PATH    "/tmp/s9.sock"
#define UDS_READ_BUF   1024

struct s9_pn {
	s9_pn_config_t config;

	pn_param_t root;
	pn_server_t *server;
	pn_uds_t *uds;
};

static void on_recv(const uint8_t *data, uint32_t len, void *ctx) {
	pn_server_feed((pn_server_t *)ctx, data, len);
}

s9_pn_t *s9_pn_new(const s9_pn_config_t *config) {
	s9_pn_t *pn = malloc(sizeof(s9_pn_t));
	CHECK_ALLOC(pn, return NULL);
	memset(pn, 0, sizeof(*pn));

	pn->config = *config;
	pn->root = pn_group("s9", NULL, 0);

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
