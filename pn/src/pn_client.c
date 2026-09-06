#include <pn/pn_client.h>
#include "pn_cobs.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <pn.pb.h>
#include <string.h>

// Internal packet size — the max protobuf-encoded message size.
#define PACKET_SIZE pn_Message_size

// --- Client ---

struct pn_client {
	int (*send)(const uint8_t *buf, uint32_t len, void *ctx);
	void *send_ctx;
	pn_on_update_t on_update;
	void *on_update_ctx;
	pn_on_schema_t on_schema;
	void *on_schema_ctx;
	pn_on_manifest_t on_manifest;
	void *on_manifest_ctx;
	pn_cobs_t *cobs;
	uint8_t *encode_buf;
	uint32_t encode_buf_size;
};

static void on_packet(const uint8_t *data, uint32_t len, void *ctx);

uint32_t
pn_client_alloc_size(const pn_client_config_t *config) {
	(void)config;
	pn_cobs_config_t cobs_cfg = { .max_packet_size = PACKET_SIZE };
	uint32_t cobs_size = pn_cobs_alloc_size(&cobs_cfg);
	uint32_t encode_size = PN_COBS_MAX_ENCODED_SIZE(PACKET_SIZE);
	return sizeof(struct pn_client) + cobs_size + encode_size;
}

void
pn_client_init(pn_client_t *client, const pn_client_config_t *config) {
	pn_cobs_config_t cobs_cfg = { .max_packet_size = PACKET_SIZE };
	uint32_t cobs_size = pn_cobs_alloc_size(&cobs_cfg);
	uint32_t encode_size = PN_COBS_MAX_ENCODED_SIZE(PACKET_SIZE);

	memset(client, 0, sizeof(struct pn_client) + cobs_size + encode_size);
	client->send = config->send;
	client->send_ctx = config->send_ctx;
	client->on_update = config->on_update;
	client->on_update_ctx = config->on_update_ctx;
	client->on_schema = config->on_schema;
	client->on_schema_ctx = config->on_schema_ctx;
	client->on_manifest = config->on_manifest;
	client->on_manifest_ctx = config->on_manifest_ctx;

	// Partition trailing storage
	uint8_t *trailing = (uint8_t *)client + sizeof(struct pn_client);

	client->cobs = (pn_cobs_t *)trailing;
	cobs_cfg.on_packet = on_packet;
	cobs_cfg.ctx = client;
	pn_cobs_init(client->cobs, &cobs_cfg);

	client->encode_buf = trailing + cobs_size;
	client->encode_buf_size = encode_size;
}

// --- Send (internal) ---

static int
send_msg(pn_client_t *client, const pn_Message *pb) {
	if (!client->send)
		return -1;

	// Protobuf encode
	uint8_t pb_buf[pn_Message_size];
	pb_ostream_t stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
	if (!pb_encode(&stream, pn_Message_fields, pb))
		return -1;

	// COBS encode
	uint32_t out_len = client->encode_buf_size;
	if (pn_cobs_encode(client->cobs, pb_buf, (uint32_t)stream.bytes_written,
					   client->encode_buf, &out_len))
		return -1;

	return client->send(client->encode_buf, out_len, client->send_ctx);
}

// --- Set ---

int
pn_client_set_float(pn_client_t *client, const char *path, float val) {
	pn_Message pb = pn_Message_init_zero;
	pb.which_msg = pn_Message_set_tag;
	strncpy(pb.msg.set.path, path, sizeof(pb.msg.set.path) - 1);
	pb.msg.set.has_value = true;
	pb.msg.set.value.which_kind = pn_Value_float_val_tag;
	pb.msg.set.value.kind.float_val = val;
	return send_msg(client, &pb);
}

int
pn_client_set_int(pn_client_t *client, const char *path, int32_t val) {
	pn_Message pb = pn_Message_init_zero;
	pb.which_msg = pn_Message_set_tag;
	strncpy(pb.msg.set.path, path, sizeof(pb.msg.set.path) - 1);
	pb.msg.set.has_value = true;
	pb.msg.set.value.which_kind = pn_Value_int_val_tag;
	pb.msg.set.value.kind.int_val = val;
	return send_msg(client, &pb);
}

int
pn_client_set_bool(pn_client_t *client, const char *path, bool val) {
	pn_Message pb = pn_Message_init_zero;
	pb.which_msg = pn_Message_set_tag;
	strncpy(pb.msg.set.path, path, sizeof(pb.msg.set.path) - 1);
	pb.msg.set.has_value = true;
	pb.msg.set.value.which_kind = pn_Value_bool_val_tag;
	pb.msg.set.value.kind.bool_val = val;
	return send_msg(client, &pb);
}

int
pn_client_set_enum(pn_client_t *client, const char *path, uint32_t val) {
	pn_Message pb = pn_Message_init_zero;
	pb.which_msg = pn_Message_set_tag;
	strncpy(pb.msg.set.path, path, sizeof(pb.msg.set.path) - 1);
	pb.msg.set.has_value = true;
	pb.msg.set.value.which_kind = pn_Value_enum_val_tag;
	pb.msg.set.value.kind.enum_val = val;
	return send_msg(client, &pb);
}

int
pn_client_set_string(pn_client_t *client, const char *path, const char *val) {
	pn_Message pb = pn_Message_init_zero;
	pb.which_msg = pn_Message_set_tag;
	strncpy(pb.msg.set.path, path, sizeof(pb.msg.set.path) - 1);
	pb.msg.set.has_value = true;
	pb.msg.set.value.which_kind = pn_Value_string_val_tag;
	strncpy(pb.msg.set.value.kind.string_val, val,
			sizeof(pb.msg.set.value.kind.string_val) - 1);
	return send_msg(client, &pb);
}

// --- Get / Action ---

int
pn_client_get(pn_client_t *client, const char *path) {
	pn_Message pb = pn_Message_init_zero;
	pb.which_msg = pn_Message_get_tag;
	strncpy(pb.msg.get.path, path, sizeof(pb.msg.get.path) - 1);
	return send_msg(client, &pb);
}

int
pn_client_action(pn_client_t *client, const char *path) {
	pn_Message pb = pn_Message_init_zero;
	pb.which_msg = pn_Message_set_tag;
	strncpy(pb.msg.set.path, path, sizeof(pb.msg.set.path) - 1);
	// No value — server dispatches as PN_ACTION
	return send_msg(client, &pb);
}

// --- Schema / Manifest ---

int
pn_client_get_schema(pn_client_t *client, const char *root) {
	pn_Message pb = pn_Message_init_zero;
	pb.which_msg = pn_Message_schema_get_tag;
	if (root)
		strncpy(pb.msg.schema_get.root, root, sizeof(pb.msg.schema_get.root) - 1);
	return send_msg(client, &pb);
}

int
pn_client_get_manifest(pn_client_t *client, const char *root) {
	if (!root)
		return -1;
	pn_Message pb = pn_Message_init_zero;
	pb.which_msg = pn_Message_manifest_get_tag;
	strncpy(pb.msg.manifest_get.root, root, sizeof(pb.msg.manifest_get.root) - 1);
	return send_msg(client, &pb);
}

// --- Receive ---

static void
deliver_update(pn_client_t *client, const pn_Update *update) {
	if (!update->has_value)
		return;

	pn_value_t val = {0};
	pn_type_t type;

	switch (update->value.which_kind) {
	case pn_Value_bool_val_tag:   val.b = update->value.kind.bool_val;   type = PN_BOOL;   break;
	case pn_Value_int_val_tag:    val.i = update->value.kind.int_val;    type = PN_INT;    break;
	case pn_Value_float_val_tag:  val.f = update->value.kind.float_val;  type = PN_FLOAT;  break;
	case pn_Value_enum_val_tag:   val.e = update->value.kind.enum_val;   type = PN_ENUM;   break;
	case pn_Value_string_val_tag: val.s = update->value.kind.string_val; type = PN_STRING; break;
	default: return;
	}

	client->on_update(update->path, &val, type, client->on_update_ctx);
}

static void
on_packet(const uint8_t *data, uint32_t len, void *ctx) {
	pn_client_t *client = (pn_client_t *)ctx;

	pn_Message pb = pn_Message_init_zero;
	pb_istream_t stream = pb_istream_from_buffer(data, len);
	if (!pb_decode(&stream, pn_Message_fields, &pb))
		return;

	switch (pb.which_msg) {
	case pn_Message_update_tag:
		if (client->on_update)
			deliver_update(client, &pb.msg.update);
		break;

	case pn_Message_schema_tag:
		if (client->on_schema)
			client->on_schema(pb.msg.schema.hash, pb.msg.schema.root,
							  client->on_schema_ctx);
		break;

	case pn_Message_manifest_chunk_tag:
		if (client->on_manifest)
			client->on_manifest(pb.msg.manifest_chunk.data.bytes,
								pb.msg.manifest_chunk.data.size,
								pb.msg.manifest_chunk.last,
								client->on_manifest_ctx);
		break;

	default:
		break;
	}
}

// --- Feed ---

int pn_client_feed(pn_client_t *client, const uint8_t *data, uint32_t len) {
	return pn_cobs_feed(client->cobs, data, len);
}
