#include <pn/pn_manifest.h>
#include "pn_param.h"
#include "pn_fnv.h"
#include <string.h>

#define HEADER_PREFIX "# pn manifest v1 "
#define HEADER_HASH   " fnv1a32="

// Longest path a message can carry: PN_MAX_PATH includes the terminator.
#define MAX_PATH_LEN (PN_MAX_PATH - 1)

static const char *const type_names[] = {
	[PN_NONE]   = "group",  // never written: groups produce no entry
	[PN_ACTION] = "action",
	[PN_BOOL]   = "bool",
	[PN_INT]    = "int",
	[PN_FLOAT]  = "float",
	[PN_ENUM]   = "enum",
	[PN_STRING] = "string",
};

_Static_assert(sizeof(type_names) / sizeof(type_names[0]) == PN_STRING + 1,
	"type_names out of sync with pn_type_t");

// --- Name rules ---

static int
name_ok(const char *s) {
	if (!s || !*s)
		return 0;
	for (; *s; s++) {
		unsigned char b = (unsigned char)*s;
		if (b <= 0x20 || b == 0x7F || b == '/' || b == '#' || b == '|')
			return 0;
	}
	return 1;
}

// Join the names on a walk path with '/'. buf must hold PN_MAX_PATH bytes.
// Returns the length, or -1 if the path exceeds MAX_PATH_LEN.
static int
join_path(const pn_param_t **path, uint32_t depth, char *buf) {
	uint32_t pos = 0;
	for (uint32_t i = 0; i <= depth; i++) {
		uint32_t n = (uint32_t)strlen(path[i]->node.name);
		uint32_t sep = i > 0 ? 1 : 0;
		if (pos + sep + n > MAX_PATH_LEN)
			return -1;
		if (sep)
			buf[pos++] = '/';
		memcpy(buf + pos, path[i]->node.name, n);
		pos += n;
	}
	return (int)pos;
}

// --- Validation ---

typedef struct {
	pn_schema_result_t result;
	const pn_param_t *offender;
} validation_t;

static int
validate_visitor(const pn_param_t *param, const pn_param_t **path,
				 uint32_t depth, void *ctx) {
	validation_t *v = ctx;
	v->offender = param;

	if ((unsigned)param->node.type > PN_STRING)
		return v->result = PN_SCHEMA_BAD_TYPE;
	if (!name_ok(param->node.name))
		return v->result = PN_SCHEMA_BAD_NAME;
	if (param->num_children > 0 && param->node.type != PN_NONE)
		return v->result = PN_SCHEMA_NOT_GROUP;

	if (param->node.type == PN_ENUM) {
		const pn_enum_meta_t *m = &param->node.meta.enum_meta;
		if (m->count == 0)
			return v->result = PN_SCHEMA_EMPTY_ENUM;
		for (uint32_t i = 0; i < m->count; i++) {
			if (!m->labels || !name_ok(m->labels[i]))
				return v->result = PN_SCHEMA_BAD_LABEL;
		}
	}

	char buf[PN_MAX_PATH];
	if (join_path(path, depth, buf) < 0)
		return v->result = PN_SCHEMA_PATH_TOO_LONG;

	// Children with NULL names are skipped here and rejected when visited.
	for (uint32_t i = 1; i < param->num_children; i++) {
		const char *name = param->children[i].node.name;
		if (!name)
			continue;
		for (uint32_t j = 0; j < i; j++) {
			const char *other = param->children[j].node.name;
			if (other && strcmp(name, other) == 0) {
				v->offender = &param->children[i];
				return v->result = PN_SCHEMA_DUPLICATE_NAME;
			}
		}
	}

	return 0;
}

// --- Emission ---

typedef struct {
	pn_sink_t sink;
	void *ctx;
	int rc;  // first nonzero sink result; once set, nothing more is emitted
} emitter_t;

static void
emit(emitter_t *e, const void *buf, uint32_t len) {
	if (e->rc == 0 && len > 0)
		e->rc = e->sink((const uint8_t *)buf, len, e->ctx);
}

static void
emit_str(emitter_t *e, const char *s) {
	emit(e, s, (uint32_t)strlen(s));
}

static void
emit_u32(emitter_t *e, uint32_t v) {
	char buf[10];
	uint32_t n = 0;
	do {
		buf[sizeof(buf) - ++n] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	emit(e, buf + sizeof(buf) - n, n);
}

static void
emit_i32(emitter_t *e, int32_t v) {
	if (v < 0) {
		emit(e, "-", 1);
		emit_u32(e, (uint32_t)(-(v + 1)) + 1u);
	} else {
		emit_u32(e, (uint32_t)v);
	}
}

static void
emit_hex32(emitter_t *e, uint32_t v) {
	static const char digits[] = "0123456789abcdef";
	char buf[8];
	for (int i = 0; i < 8; i++)
		buf[i] = digits[(v >> (28 - 4 * i)) & 0xF];
	emit(e, buf, sizeof(buf));
}

static void
emit_f32(emitter_t *e, float f) {
	uint32_t bits;
	memcpy(&bits, &f, sizeof(bits));
	emit(e, "0x", 2);
	emit_hex32(e, bits);
}

static int
entry_visitor(const pn_param_t *param, const pn_param_t **path,
			  uint32_t depth, void *ctx) {
	emitter_t *e = ctx;
	if (param->node.type == PN_NONE)
		return 0;

	char buf[PN_MAX_PATH];
	int len = join_path(path, depth, buf);
	if (len < 0)
		return -1;

	emit(e, buf, (uint32_t)len);
	emit(e, " ", 1);
	emit_str(e, type_names[param->node.type]);

	const pn_meta_t *m = &param->node.meta;
	switch (param->node.type) {
	case PN_INT:
		emit(e, " ", 1); emit_i32(e, m->int_meta.min);
		emit(e, " ", 1); emit_i32(e, m->int_meta.max);
		emit(e, " ", 1); emit_i32(e, m->int_meta.step);
		break;
	case PN_FLOAT:
		emit(e, " ", 1); emit_f32(e, m->float_meta.min);
		emit(e, " ", 1); emit_f32(e, m->float_meta.max);
		emit(e, " ", 1); emit_f32(e, m->float_meta.step);
		break;
	case PN_ENUM:
		emit(e, " ", 1);
		for (uint32_t i = 0; i < m->enum_meta.count; i++) {
			if (i > 0)
				emit(e, "|", 1);
			emit_str(e, m->enum_meta.labels[i]);
		}
		break;
	case PN_STRING:
		emit(e, " ", 1); emit_u32(e, m->string_meta.max_len);
		break;
	default:
		break;
	}
	emit(e, "\n", 1);
	return e->rc;
}

// Write the canonical body: one entry per leaf, in walk order.
// Returns 0, the sink's nonzero result, or -1 if the walk refused the tree.
static int
write_entries(const pn_param_t *root, pn_sink_t sink, void *ctx) {
	emitter_t e = { sink, ctx, 0 };
	const pn_param_t *path[PN_MAX_DEPTH];
	int rc = pn_param_walk(root, entry_visitor, &e, path, PN_MAX_DEPTH);
	if (e.rc)
		return e.rc;
	return rc ? -1 : 0;
}

static int
hash_sink(const uint8_t *buf, uint32_t len, void *ctx) {
	uint32_t *hash = ctx;
	*hash = pn_fnv1a32(*hash, buf, len);
	return 0;
}

// --- Public API ---

pn_schema_result_t
pn_schema_validate(const pn_param_t *root, const pn_param_t **offender) {
	validation_t v = { PN_SCHEMA_OK, NULL };

	if (!root) {
		v.result = PN_SCHEMA_NO_ROOT;
	} else {
		const pn_param_t *path[PN_MAX_DEPTH];
		int rc = pn_param_walk(root, validate_visitor, &v, path, PN_MAX_DEPTH);
		if (rc && v.result == PN_SCHEMA_OK) {
			// The walk stopped on its own: it refused to descend past PN_MAX_DEPTH.
			v.result = PN_SCHEMA_TOO_DEEP;
			v.offender = NULL;
		}
	}

	if (v.result == PN_SCHEMA_OK)
		v.offender = NULL;
	if (offender)
		*offender = v.offender;
	return v.result;
}

int
pn_schema_hash(const pn_param_t *root, uint32_t *hash) {
	if (pn_schema_validate(root, NULL) != PN_SCHEMA_OK)
		return -1;

	uint32_t h = PN_FNV1A32_INIT;
	if (write_entries(root, hash_sink, &h))
		return -1;
	*hash = h;
	return 0;
}

int
pn_manifest_write(const pn_param_t *root, pn_sink_t sink, void *ctx) {
	if (!sink)
		return -1;

	uint32_t hash;
	if (pn_schema_hash(root, &hash))
		return -1;

	emitter_t e = { sink, ctx, 0 };
	emit_str(&e, HEADER_PREFIX);
	emit_str(&e, root->node.name);
	emit_str(&e, HEADER_HASH);
	emit_hex32(&e, hash);
	emit(&e, "\n", 1);
	if (e.rc)
		return e.rc;

	return write_entries(root, sink, ctx);
}
