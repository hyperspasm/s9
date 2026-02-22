#ifndef pn_uds_h
#define pn_uds_h

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#define PN_HAS_UDS 1
#endif

#include <stdint.h>

typedef struct pn_uds pn_uds_t;

typedef struct {
	const char *path;
	uint32_t read_buf_size;
	void (*on_recv)(const uint8_t *data, uint32_t len, void *ctx);
	void *on_recv_ctx;
} pn_uds_config_t;

uint32_t pn_uds_alloc_size(const pn_uds_config_t *config);
int      pn_uds_init(pn_uds_t *uds, const pn_uds_config_t *config);

int      pn_uds_listen(pn_uds_t *uds);   // server: bind + listen
int      pn_uds_connect(pn_uds_t *uds);  // client: connect

int      pn_uds_poll(pn_uds_t *uds);     // non-blocking read, calls on_recv

// Compatible with pn_server_config_t send signature. Pass pn_uds_t* as ctx.
int      pn_uds_send(const uint8_t *buf, uint32_t len, void *ctx);

void     pn_uds_close(pn_uds_t *uds);

#endif /* pn_uds_h */
