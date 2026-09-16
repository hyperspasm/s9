#ifndef s9_ws_h
#define s9_ws_h

#include <stdint.h>

// The websocket hub: every connected client's binary messages are handed to
// on_recv, and every message given to s9_ws_send goes to every connected
// client. The same port serves the web interface over HTTP, from the files
// built into the binary or from a directory, and the manifest at
// /s9.manifest.
//
// One pn packet travels as one binary websocket message. A message that does
// not end with the pn frame delimiter, exceeds the size limit, or is not binary
// is dropped. Clients request the websocket subprotocol "pn". A client whose
// requests produce replies faster than the hub writes them is paused until
// the queue drains; a client that stops reading is disconnected.
//
// Single-threaded: create, update, send, and free from one thread, and call
// update once per frame; it never blocks.

typedef struct s9_ws s9_ws_t;

typedef struct {
	uint16_t port;
	const char *www_dir;          // NULL: serve the files built into the binary
	const char *manifest;         // served at /s9.manifest
	uint32_t manifest_len;
	void (*on_recv)(const uint8_t *data, uint32_t len, void *ctx);
	void *on_recv_ctx;
} s9_ws_config_t;

s9_ws_t *s9_ws_new(const s9_ws_config_t *config);
void s9_ws_free(s9_ws_t **ws);

// service the connections; never blocks
void s9_ws_update(s9_ws_t *ws);

// queue a message for every connected client. Same signature as a pn send
// callback; pass the s9_ws_t as ctx. Returns 0, or -1 if the message was
// dropped.
int s9_ws_send(const uint8_t *data, uint32_t len, void *ctx);

// number of connected clients
uint32_t s9_ws_clients(const s9_ws_t *ws);

#endif /* s9_ws_h */
