#include "s9_ws.h"
#include "s9.h"
#include "s9_www.h"

#include <libwebsockets.h>

#include <stdlib.h>
#include <string.h>

#define MSG_MAX      1024   // largest message carried, in bytes
#define RING_COUNT   256    // ring slots; one is always unused, so the hub holds RING_COUNT - 1
                            // messages for its slowest client. Messages are queued while a
                            // request is handled and written on the next update, so this must
                            // exceed the replies one request produces.
#define RING_LOW     64     // free slots at which the client whose request is being handled is
                            // stopped from sending more, and ...
#define RING_HIGH    128    // ... free slots at which it is allowed to send again
#define HTTP_CHUNK   4096   // bytes of a document written per HTTP writeable callback
#define MANIFEST_URL "/s9.manifest"
#define URI_MAX      256

// -- types

typedef struct {
	unsigned char buf[LWS_PRE + MSG_MAX];
	uint32_t len;
} msg_t;

// a websocket client: per-session data of the "pn" protocol
typedef struct session {
	struct session *next;
	struct lws *wsi;
	uint32_t tail;              // position in the ring
	int culled;                 // removed from the list; the close is pending
	int throttled;              // its input is paused until the ring has room
	unsigned char rx[MSG_MAX];  // message being reassembled from its fragments
	uint32_t rx_len;
	int rx_overflow;
} session_t;

// an HTTP transaction: per-session data of the "http" protocol
typedef struct {
	const unsigned char *data;
	uint32_t len;
	uint32_t pos;
} http_session_t;

struct s9_ws {
	s9_ws_config_t config;
	struct lws_context *context;
	struct lws_ring *ring;
	session_t *sessions;
	uint32_t n_sessions;
	session_t *receiving;       // the session whose message on_recv is handling, else NULL
	struct lws_protocols protocols[3];
	struct lws_http_mount manifest_mount;
	struct lws_http_mount files_mount;
};

static s9_ws_t *ws_of(struct lws *wsi) {
	return lws_context_user(lws_get_context(wsi));
}

// -- logging

static void lws_log_line(int level, const char *line) {
	char buf[256];
	snprintf(buf, sizeof(buf), "%s", line);
	buf[strcspn(buf, "\r\n")] = '\0';
	if (level & LLL_ERR) S9_LOG_ERROR("ws: %s", buf);
	else S9_LOG_WARNING("ws: %s", buf);
}

// -- ring

// After sessions leave, the ring's oldest tail must move up to the slowest
// remaining session, or to the head when none remain, or the ring never
// reclaims its slots.
static void retire_behind_sessions(s9_ws_t *ws) {
	if (!ws->sessions) {
		lws_ring_consume(ws->ring, NULL, NULL,
						 lws_ring_get_count_waiting_elements(ws->ring, NULL));
		return;
	}
	uint32_t oldest = 0;
	size_t most = 0;
	int first = 1;
	for (session_t *s = ws->sessions; s; s = s->next) {
		size_t waiting = lws_ring_get_count_waiting_elements(ws->ring, &s->tail);
		if (first || waiting > most) { most = waiting; oldest = s->tail; first = 0; }
	}
	lws_ring_update_oldest_tail(ws->ring, oldest);
}

// Let throttled clients send again once the ring has drained enough.
static void unthrottle_if_room(s9_ws_t *ws) {
	if (lws_ring_get_count_free_elements(ws->ring) < RING_HIGH)
		return;
	for (session_t *s = ws->sessions; s; s = s->next) {
		if (s->throttled) {
			lws_rx_flow_control(s->wsi, 1);
			s->throttled = 0;
		}
	}
}

static void remove_session(s9_ws_t *ws, session_t *pss) {
	lws_ll_fwd_remove(session_t, next, pss, ws->sessions);
	ws->n_sessions--;
	retire_behind_sessions(ws);
	unthrottle_if_room(ws);
}

// Disconnect the sessions holding the ring's oldest slot whose socket is
// choked. A session that is merely waiting for its next writeable callback is
// left alone. Returns the number culled.
static int cull_choked(s9_ws_t *ws) {
	uint32_t oldest = lws_ring_get_oldest_tail(ws->ring);
	int culled = 0;
	session_t *s = ws->sessions;
	while (s) {
		session_t *next = s->next;
		if (s->tail == oldest && lws_send_pipe_choked(s->wsi)) {
			S9_LOG_WARNING("ws: disconnecting a client that stopped reading");
			lws_set_timeout(s->wsi, PENDING_TIMEOUT_LAGGING, LWS_TO_KILL_ASYNC);
			s->culled = 1;
			remove_session(ws, s);
			culled++;
		}
		s = next;
	}
	return culled;
}

// -- websocket protocol

static int callback_pn(struct lws *wsi, enum lws_callback_reasons reason,
					   void *user, void *in, size_t len) {
	s9_ws_t *ws = ws_of(wsi);
	session_t *pss = user;

	switch (reason) {
	case LWS_CALLBACK_ESTABLISHED:
		pss->wsi = wsi;
		// start at the head: messages queued before the client arrived are not for it
		pss->tail = lws_ring_get_oldest_tail(ws->ring);
		lws_ring_consume(ws->ring, &pss->tail, NULL,
						 lws_ring_get_count_waiting_elements(ws->ring, &pss->tail));
		lws_ll_fwd_insert(pss, next, ws->sessions);
		ws->n_sessions++;
		S9_LOG_INFO("ws: client connected (%u)", ws->n_sessions);
		break;

	case LWS_CALLBACK_CLOSED:
		if (!pss->culled)
			remove_session(ws, pss);
		S9_LOG_INFO("ws: client disconnected (%u)", ws->n_sessions);
		break;

	case LWS_CALLBACK_RECEIVE:
		if (pss->culled)
			break;
		if (!lws_frame_is_binary(wsi)) {
			pss->rx_len = 0;
			break;
		}
		if (lws_is_first_fragment(wsi)) {
			pss->rx_len = 0;
			pss->rx_overflow = 0;
		}
		if (pss->rx_len + len > sizeof(pss->rx)) {
			pss->rx_overflow = 1;
		} else {
			memcpy(pss->rx + pss->rx_len, in, len);
			pss->rx_len += (uint32_t)len;
		}
		if (lws_is_final_fragment(wsi)) {
			if (pss->rx_overflow)
				S9_LOG_WARNING("ws: dropped a message longer than %u bytes", (unsigned)sizeof(pss->rx));
			else if (pss->rx_len == 0 || pss->rx[pss->rx_len - 1] != 0)
				S9_LOG_WARNING("ws: dropped a message that does not end with the frame delimiter");
			else if (ws->config.on_recv) {
				ws->receiving = pss;
				ws->config.on_recv(pss->rx, pss->rx_len, ws->config.on_recv_ctx);
				ws->receiving = NULL;
			}
			pss->rx_len = 0;
		}
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE: {
		if (pss->culled)
			break;
		const msg_t *msg;
		while ((msg = lws_ring_get_element(ws->ring, &pss->tail)) != NULL) {
			int n = lws_write(wsi, (unsigned char *)msg->buf + LWS_PRE, msg->len, LWS_WRITE_BINARY);
			if (n < (int)msg->len)
				return -1;
			lws_ring_consume_and_update_oldest_tail(ws->ring, session_t, &pss->tail, 1,
													ws->sessions, tail, next);
			if (lws_send_pipe_choked(wsi) || lws_partial_buffered(wsi)) {
				lws_callback_on_writable(wsi);
				break;
			}
		}
		unthrottle_if_room(ws);
		break;
	}

	default:
		break;
	}
	return 0;
}

// -- http protocol

static int serve_document(struct lws *wsi, http_session_t *tx,
						  const unsigned char *data, uint32_t len, const char *mime) {
	unsigned char headers[LWS_PRE + 512];
	unsigned char *start = headers + LWS_PRE, *p = start, *end = headers + sizeof(headers);

	if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK, mime, len, &p, end))
		return 1;
	if (lws_finalize_write_http_header(wsi, start, &p, end))
		return 1;

	tx->data = data;
	tx->len = len;
	tx->pos = 0;
	lws_callback_on_writable(wsi);
	return 0;
}

static int callback_http(struct lws *wsi, enum lws_callback_reasons reason,
						 void *user, void *in, size_t len) {
	s9_ws_t *ws = ws_of(wsi);
	http_session_t *tx = user;

	switch (reason) {
	case LWS_CALLBACK_HTTP: {
		// Route on the request's own URI: `in` is only the remainder after a
		// matched mount, and the manifest mount also matches paths below it.
		char uri[URI_MAX];
		if (lws_hdr_copy(wsi, uri, sizeof(uri), WSI_TOKEN_GET_URI) <= 0)
			return lws_callback_http_dummy(wsi, reason, user, in, len);
		if (strcmp(uri, MANIFEST_URL) == 0)
			return serve_document(wsi, tx, (const unsigned char *)ws->config.manifest,
								  ws->config.manifest_len, "text/plain");
		if (ws->config.www_dir == NULL) {
			const s9_www_file_t *f = s9_www_find(uri);
			if (f)
				return serve_document(wsi, tx, f->data, f->size, f->mime);
		}
		return lws_callback_http_dummy(wsi, reason, user, in, len);
	}

	case LWS_CALLBACK_HTTP_WRITEABLE: {
		if (!tx->data)
			break;
		unsigned char buf[LWS_PRE + HTTP_CHUNK];
		uint32_t n = tx->len - tx->pos;
		if (n > HTTP_CHUNK) n = HTTP_CHUNK;
		memcpy(buf + LWS_PRE, tx->data + tx->pos, n);
		int last = tx->pos + n == tx->len;
		if (lws_write(wsi, buf + LWS_PRE, n, last ? LWS_WRITE_HTTP_FINAL : LWS_WRITE_HTTP) < 0)
			return 1;
		tx->pos += n;
		if (!last) {
			lws_callback_on_writable(wsi);
			break;
		}
		tx->data = NULL;
		if (lws_http_transaction_completed(wsi))
			return -1;
		break;
	}

	// a websocket client that did not request the pn subprotocol lands here
	case LWS_CALLBACK_ESTABLISHED:
		S9_LOG_WARNING("ws: closing a client that did not request the pn subprotocol");
		return -1;

	default:
		return lws_callback_http_dummy(wsi, reason, user, in, len);
	}
	return 0;
}

// -- public

s9_ws_t *s9_ws_new(const s9_ws_config_t *config) {
	s9_ws_t *ws = malloc(sizeof(s9_ws_t));
	CHECK_ALLOC(ws, return NULL);
	memset(ws, 0, sizeof(*ws));
	ws->config = *config;

	ws->ring = lws_ring_create(sizeof(msg_t), RING_COUNT, NULL);
	CHECK_ALLOC(ws->ring, free(ws); return NULL);

	ws->protocols[0] = (struct lws_protocols){
		.name = "http",
		.callback = callback_http,
		.per_session_data_size = sizeof(http_session_t),
	};
	ws->protocols[1] = (struct lws_protocols){
		.name = "pn",
		.callback = callback_pn,
		.per_session_data_size = sizeof(session_t),
		.rx_buffer_size = MSG_MAX,
		.tx_packet_size = MSG_MAX,
	};

	ws->manifest_mount = (struct lws_http_mount){
		.mountpoint = MANIFEST_URL,
		.mountpoint_len = (unsigned char)strlen(MANIFEST_URL),
		.origin_protocol = LWSMPRO_CALLBACK,
		.protocol = "http",
	};
	if (ws->config.www_dir) {
		ws->files_mount = (struct lws_http_mount){
			.mountpoint = "/",
			.mountpoint_len = 1,
			.origin = ws->config.www_dir,
			.def = "index.html",
			.origin_protocol = LWSMPRO_FILE,
		};
		ws->manifest_mount.mount_next = &ws->files_mount;
	}

	lws_set_log_level(LLL_ERR | LLL_WARN, lws_log_line);

	struct lws_context_creation_info info;
	memset(&info, 0, sizeof(info));
	info.port = ws->config.port;
	info.protocols = ws->protocols;
	info.mounts = &ws->manifest_mount;
	info.user = ws;
	info.gid = -1;
	info.uid = -1;

	ws->context = lws_create_context(&info);
	if (!ws->context) {
		S9_LOG_ERROR("ws: failed to listen on port %u", (unsigned)ws->config.port);
		lws_ring_destroy(ws->ring);
		free(ws);
		return NULL;
	}
	return ws;
}

void s9_ws_free(s9_ws_t **ws) {
	if (ws && *ws) {
		lws_context_destroy((*ws)->context);
		lws_ring_destroy((*ws)->ring);
		free(*ws);
		*ws = NULL;
	}
}

void s9_ws_update(s9_ws_t *ws) {
	// A negative timeout is the non-blocking form: the library replaces any
	// timeout of zero or more with a wait for the next event
	// (lib/plat/unix/unix-service.c, versions 4.1 and 4.3).
	lws_service(ws->context, -1);
}

int s9_ws_send(const uint8_t *data, uint32_t len, void *ctx) {
	s9_ws_t *ws = ctx;
	if (len > MSG_MAX) {
		S9_LOG_WARNING("ws: dropped a %u byte message; the limit is %u", (unsigned)len, MSG_MAX);
		return -1;
	}
	if (!ws->sessions)
		return 0;

	if (lws_ring_get_count_free_elements(ws->ring) == 0) {
		cull_choked(ws);
		if (!ws->sessions)
			return 0;
		if (lws_ring_get_count_free_elements(ws->ring) == 0) {
			S9_LOG_WARNING("ws: dropped a message; %u are already queued", RING_COUNT - 1);
			return -1;
		}
	}

	msg_t msg;
	memcpy(msg.buf + LWS_PRE, data, len);
	msg.len = len;
	lws_ring_insert(ws->ring, &msg, 1);

	// The replies to one request are queued before any are written. If they
	// are filling the ring, stop reading that client's further requests until
	// the ring drains, rather than dropping replies to everyone.
	session_t *r = ws->receiving;
	if (r && !r->throttled && !r->culled && lws_ring_get_count_free_elements(ws->ring) < RING_LOW) {
		lws_rx_flow_control(r->wsi, 0);
		r->throttled = 1;
	}

	for (session_t *s = ws->sessions; s; s = s->next)
		lws_callback_on_writable(s->wsi);
	return 0;
}

uint32_t s9_ws_clients(const s9_ws_t *ws) {
	return ws->n_sessions;
}
