#include <pn/pn_server.h>
#include <pn/pn_manifest.h>
#include "pn_param.h"
#include "pn_cobs.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <pn.pb.h>
#include <string.h>

// Verify protocol limits match the generated protobuf structs (from pn.options)
_Static_assert(sizeof(((pn_Set *)0)->path) == PN_MAX_PATH,
	"PN_MAX_PATH out of sync with pn.options");
_Static_assert(sizeof(((pn_Value *)0)->kind.string_val) == PN_MAX_STRING,
	"PN_MAX_STRING out of sync with pn.options");

// Internal packet size — the max protobuf-encoded message size.
// All COBS buffers are sized from this.
#define PACKET_SIZE pn_Message_size

// --- Message (internal) ---

typedef enum {
	PN_MSG_SET,
	PN_MSG_GET,
	PN_MSG_UPDATE,
} pn_msg_type_t;

typedef struct {
	pn_msg_type_t type;
	const char *path;
	pn_value_t value;
	pn_type_t value_type;
} pn_msg_t;

// --- Server ---

struct pn_server {
	pn_param_t *root;
	int (*send)(const uint8_t *buf, uint32_t len, void *ctx);
	void *send_ctx;
	pn_cobs_t *cobs;
	uint8_t *encode_buf;
	uint32_t encode_buf_size;
	uint32_t schema_hash;
};

static void on_packet(const uint8_t *data, uint32_t len, void *ctx);

uint32_t
pn_server_alloc_size(const pn_server_config_t *config) {
	(void)config;
	pn_cobs_config_t cobs_cfg = { .max_packet_size = PACKET_SIZE };
	uint32_t cobs_size = pn_cobs_alloc_size(&cobs_cfg);
	uint32_t encode_size = PN_COBS_MAX_ENCODED_SIZE(PACKET_SIZE);
	return sizeof(struct pn_server) + cobs_size + encode_size;
}

void
pn_server_init(pn_server_t *server, const pn_server_config_t *config) {
	pn_cobs_config_t cobs_cfg = { .max_packet_size = PACKET_SIZE };
	uint32_t cobs_size = pn_cobs_alloc_size(&cobs_cfg);
	uint32_t encode_size = PN_COBS_MAX_ENCODED_SIZE(PACKET_SIZE);

	memset(server, 0, sizeof(struct pn_server) + cobs_size + encode_size);
	server->root = config->root;
	server->send = config->send;
	server->send_ctx = config->send_ctx;

	// Partition trailing storage
	uint8_t *trailing = (uint8_t *)server + sizeof(struct pn_server);

	server->cobs = (pn_cobs_t *)trailing;
	cobs_cfg.on_packet = on_packet;
	cobs_cfg.ctx = server;
	pn_cobs_init(server->cobs, &cobs_cfg);

	server->encode_buf = trailing + cobs_size;
	server->encode_buf_size = encode_size;
}

static void
bind_tree(pn_server_t *server, pn_param_t *param, pn_param_t *parent) {
	param->bind.server = server;
	param->bind.parent = parent;
	for (uint32_t i = 0; i < param->num_children; i++)
		bind_tree(server, &param->children[i], param);
}

int
pn_server_start(pn_server_t *server) {
	// pn_schema_hash() validates the tree; a tree it accepts is safe to bind.
	if (pn_schema_hash(server->root, &server->schema_hash) != 0)
		return -1;
	bind_tree(server, server->root, NULL);
	return 0;
}

uint32_t
pn_server_schema_hash(const pn_server_t *server) {
	return server->schema_hash;
}

// --- Set/Get ---

static int32_t clamp_i(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static float   clamp_f(float v, float lo, float hi)      { return v < lo ? lo : v > hi ? hi : v; }

int pn_param_set(const pn_param_t *param, const pn_value_t *val) {
	const pn_meta_t *m = &param->node.meta;
	switch (param->node.type) {
	case PN_ACTION: if (!param->set.action) return -1; param->set.action(param->ctx); return 0;
	case PN_BOOL:   if (!param->set.b) return -1; param->set.b(param->ctx, val->b); return 0;
	case PN_INT:    if (!param->set.i) return -1; param->set.i(param->ctx, clamp_i(val->i, m->int_meta.min, m->int_meta.max)); return 0;
	case PN_FLOAT:  if (!param->set.f) return -1; param->set.f(param->ctx, clamp_f(val->f, m->float_meta.min, m->float_meta.max)); return 0;
	case PN_ENUM:   if (!param->set.e || m->enum_meta.count == 0) return -1; param->set.e(param->ctx, val->e < m->enum_meta.count ? val->e : m->enum_meta.count - 1); return 0;
	case PN_STRING: if (!param->set.s) return -1; param->set.s(param->ctx, val->s); return 0;
	default: return -1;
	}
}

int pn_param_get(const pn_param_t *param, pn_value_t *val) {
	switch (param->node.type) {
	case PN_BOOL:   if (!param->get.b) return -1; val->b = param->get.b(param->ctx); return 0;
	case PN_INT:    if (!param->get.i) return -1; val->i = param->get.i(param->ctx); return 0;
	case PN_FLOAT:  if (!param->get.f) return -1; val->f = param->get.f(param->ctx); return 0;
	case PN_ENUM:   if (!param->get.e) return -1; val->e = param->get.e(param->ctx); return 0;
	case PN_STRING: if (!param->get.s) return -1; val->s = param->get.s(param->ctx); return 0;
	default: return -1;
	}
}

// --- Find ---

const pn_param_t *pn_param_find(const pn_param_t *root, const char *path) {
	if (!root || !path)
		return NULL;

	// Skip leading slashes
	while (*path == '/') path++;
	if (!*path)
		return NULL;

	// First segment must match the root name
	const char *sep = strchr(path, '/');
	uint32_t len = sep ? (uint32_t)(sep - path) : (uint32_t)strlen(path);

	if (strncmp(root->node.name, path, len) != 0
		|| root->node.name[len] != '\0')
		return NULL;

	path += len;
	const pn_param_t *param = root;

	while (*path) {
		if (*path == '/') { path++; continue; }

		sep = strchr(path, '/');
		len = sep ? (uint32_t)(sep - path) : (uint32_t)strlen(path);

		const pn_param_t *found = NULL;
		for (uint32_t i = 0; i < param->num_children; i++) {
			if (strncmp(param->children[i].node.name, path, len) == 0
				&& param->children[i].node.name[len] == '\0') {
				found = &param->children[i];
				break;
			}
		}

		if (!found)
			return NULL;

		param = found;
		path += len;
	}

	return param;
}

// --- Walk ---

static int walk_param(const pn_param_t *param,
					  const pn_param_t **path,
					  uint32_t depth,
					  uint32_t max_depth,
					  pn_param_visitor_t visit, void *ctx) {
	if (depth >= max_depth)
		return -1;

	path[depth] = param;

	int rc = visit(param, path, depth, ctx);
	if (rc)
		return rc;

	for (uint32_t i = 0; i < param->num_children; i++) {
		rc = walk_param(&param->children[i], path, depth + 1, max_depth, visit, ctx);
		if (rc)
			return rc;
	}

	return 0;
}

int pn_param_walk(const pn_param_t *root, pn_param_visitor_t visit, void *ctx,
				  const pn_param_t **path, uint32_t max_depth) {
	return walk_param(root, path, 0, max_depth, visit, ctx);
}

// --- Protobuf encode/decode (internal) ---

static int
pb_decode_msg(const uint8_t *data, uint32_t len, pn_msg_t *msg, pn_Message *pb) {
	*pb = (pn_Message)pn_Message_init_zero;
	pb_istream_t stream = pb_istream_from_buffer(data, len);

	if (!pb_decode(&stream, pn_Message_fields, pb))
		return -1;

	switch (pb->which_msg) {
	case pn_Message_set_tag:
		msg->type = PN_MSG_SET;
		msg->path = pb->msg.set.path;
		if (pb->msg.set.has_value) {
			switch (pb->msg.set.value.which_kind) {
			case pn_Value_bool_val_tag:   msg->value.b = pb->msg.set.value.kind.bool_val;   msg->value_type = PN_BOOL;   break;
			case pn_Value_int_val_tag:    msg->value.i = pb->msg.set.value.kind.int_val;    msg->value_type = PN_INT;    break;
			case pn_Value_float_val_tag:  msg->value.f = pb->msg.set.value.kind.float_val;  msg->value_type = PN_FLOAT;  break;
			case pn_Value_enum_val_tag:   msg->value.e = pb->msg.set.value.kind.enum_val;   msg->value_type = PN_ENUM;   break;
			case pn_Value_string_val_tag: msg->value.s = pb->msg.set.value.kind.string_val; msg->value_type = PN_STRING; break;
			default: return -1;
			}
		}
		return 0;

	case pn_Message_get_tag:
		msg->type = PN_MSG_GET;
		msg->path = pb->msg.get.path;
		return 0;

	default:
		return -1;
	}
}

static int
pb_encode_update(const char *path, const pn_param_t *param, const pn_value_t *val,
				 uint8_t *buf, uint32_t *len) {
	pn_Message pb = pn_Message_init_zero;
	pb.which_msg = pn_Message_update_tag;
	strncpy(pb.msg.update.path, path, sizeof(pb.msg.update.path) - 1);
	pb.msg.update.has_value = true;

	switch (param->node.type) {
	case PN_BOOL:   pb.msg.update.value.which_kind = pn_Value_bool_val_tag;  pb.msg.update.value.kind.bool_val  = val->b; break;
	case PN_INT:    pb.msg.update.value.which_kind = pn_Value_int_val_tag;   pb.msg.update.value.kind.int_val   = val->i; break;
	case PN_FLOAT:  pb.msg.update.value.which_kind = pn_Value_float_val_tag; pb.msg.update.value.kind.float_val = val->f; break;
	case PN_ENUM:   pb.msg.update.value.which_kind = pn_Value_enum_val_tag;  pb.msg.update.value.kind.enum_val  = val->e; break;
	case PN_STRING:
		pb.msg.update.value.which_kind = pn_Value_string_val_tag;
		strncpy(pb.msg.update.value.kind.string_val, val->s,
				sizeof(pb.msg.update.value.kind.string_val) - 1);
		break;
	default: return -1;
	}

	pb_ostream_t stream = pb_ostream_from_buffer(buf, *len);
	if (!pb_encode(&stream, pn_Message_fields, &pb))
		return -1;

	*len = (uint32_t)stream.bytes_written;
	return 0;
}

// --- Dispatch ---

static int
send_update(pn_server_t *server, const char *path,
			const pn_param_t *param, const pn_value_t *val) {
	if (!server->send)
		return -1;

	// Protobuf encode
	uint8_t pb_buf[pn_Message_size];
	uint32_t pb_len = sizeof(pb_buf);
	if (pb_encode_update(path, param, val, pb_buf, &pb_len))
		return -1;

	// COBS encode
	uint32_t out_len = server->encode_buf_size;
	if (pn_cobs_encode(server->cobs, pb_buf, pb_len, server->encode_buf, &out_len))
		return -1;

	return server->send(server->encode_buf, out_len, server->send_ctx);
}

// Build full path from a param by walking bind.parent up to root.
static int
build_path(const pn_param_t *param, char *buf, uint32_t buf_size) {
	// Collect ancestors (including param itself)
	const pn_param_t *chain[PN_MAX_DEPTH];
	uint32_t depth = 0;
	for (const pn_param_t *p = param; p != NULL; p = p->bind.parent) {
		if (depth >= PN_MAX_DEPTH)
			return -1;
		chain[depth++] = p;
	}

	// Write segments root-first
	uint32_t pos = 0;
	for (uint32_t i = depth; i > 0; i--) {
		const char *name = chain[i - 1]->node.name;
		uint32_t name_len = (uint32_t)strlen(name);
		if (pos + name_len + 1 >= buf_size)
			return -1;
		if (pos > 0)
			buf[pos++] = '/';
		memcpy(buf + pos, name, name_len);
		pos += name_len;
	}
	buf[pos] = '\0';
	return 0;
}

int
pn_param_notify(pn_param_t *param) {
	pn_server_t *server = param->bind.server;
	if (!server)
		return -1;

	pn_value_t val = {0};
	if (pn_param_get(param, &val) != 0)
		return -1;

	char path[PN_MAX_PATH];
	if (build_path(param, path, sizeof(path)) != 0)
		return -1;

	return send_update(server, path, param, &val);
}

typedef struct {
	pn_server_t *server;
	const char *prefix;
	uint32_t prefix_len;
} subtree_ctx_t;

static int
send_subtree_visitor(const pn_param_t *param,
					 const pn_param_t **path,
					 uint32_t depth, void *ctx) {
	subtree_ctx_t *sc = (subtree_ctx_t *)ctx;

	// Skip groups and actions
	if (param->node.type == PN_NONE || param->node.type == PN_ACTION)
		return 0;

	pn_value_t val = {0};
	if (pn_param_get(param, &val) != 0)
		return 0;

	// Build suffix from walk path (skip depth 0 — that's the subtree root,
	// already covered by the prefix)
	char path_str[PN_MAX_PATH];
	uint32_t pos = sc->prefix_len;
	if (pos >= sizeof(path_str))
		return 0;
	memcpy(path_str, sc->prefix, pos);

	for (uint32_t i = 1; i <= depth; i++) {
		uint32_t name_len = (uint32_t)strlen(path[i]->node.name);
		if (pos + 1 + name_len >= sizeof(path_str))
			return 0;
		path_str[pos++] = '/';
		memcpy(path_str + pos, path[i]->node.name, name_len);
		pos += (uint32_t)name_len;
	}
	path_str[pos] = '\0';

	send_update(sc->server, path_str, param, &val);
	return 0;
}

static void
dispatch(pn_server_t *server, const pn_msg_t *msg) {
	const pn_param_t *param = pn_param_find(server->root, msg->path);
	if (!param)
		return;

	pn_value_t val = {0};

	switch (msg->type) {
	case PN_MSG_SET:
		if (param->node.type == PN_ACTION) {
			pn_param_set(param, NULL);
		} else {
			pn_value_t coerced = msg->value;
			pn_type_t from = msg->value_type;
			pn_type_t to = param->node.type;
			if (from == to) {
				// Exact match
			} else if (from == PN_INT   && to == PN_FLOAT) { coerced.f = (float)msg->value.i;
			} else if (from == PN_FLOAT && to == PN_INT)   { coerced.i = (int32_t)msg->value.f;
			} else if (from == PN_INT   && to == PN_BOOL)  { coerced.b = msg->value.i != 0;
			} else if (from == PN_INT   && to == PN_ENUM)  { coerced.e = (uint32_t)msg->value.i;
			} else {
				return;
			}
			pn_param_set(param, &coerced);
			if (pn_param_get(param, &val) == 0)
				send_update(server, msg->path, param, &val);
		}
		break;

	case PN_MSG_GET:
		if (param->node.type == PN_NONE) {
			// Group: send updates for all leaf params in subtree
			uint32_t plen = (uint32_t)strlen(msg->path);
			while (plen > 0 && msg->path[plen - 1] == '/') plen--;
			subtree_ctx_t sc = { .server = server, .prefix = msg->path,
								 .prefix_len = plen };
			const pn_param_t *walk_path[PN_MAX_DEPTH];
			pn_param_walk(param, send_subtree_visitor, &sc,
						  walk_path, PN_MAX_DEPTH);
		} else {
			if (pn_param_get(param, &val) == 0)
				send_update(server, msg->path, param, &val);
		}
		break;

	default:
		break;
	}
}

static void
on_packet(const uint8_t *data, uint32_t len, void *ctx) {
	pn_server_t *server = (pn_server_t *)ctx;
	pn_Message pb;
	pn_msg_t msg = {0};

	if (pb_decode_msg(data, len, &msg, &pb) == 0)
		dispatch(server, &msg);
}

// --- Feed ---

int pn_server_feed(pn_server_t *server, const uint8_t *data, uint32_t len) {
	return pn_cobs_feed(server->cobs, data, len);
}
