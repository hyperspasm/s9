#ifndef pn_server_h
#define pn_server_h

#include "pn_node.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Function pointers ---

typedef union {
	void (*action)(void *ctx);
	void (*b)(void *ctx, bool val);
	void (*i)(void *ctx, int32_t val);
	void (*f)(void *ctx, float val);
	void (*e)(void *ctx, uint32_t val);
	void (*s)(void *ctx, const char *val);
} pn_set_fn_t;

typedef union {
	bool (*b)(void *ctx);
	int32_t (*i)(void *ctx);
	float (*f)(void *ctx);
	uint32_t (*e)(void *ctx);
	const char *(*s)(void *ctx);
} pn_get_fn_t;

// --- Param (server-side node) ---

typedef struct pn_param {
	pn_node_t node;
	struct pn_param *children;
	uint32_t num_children;
	void *ctx;
	pn_set_fn_t set;
	pn_get_fn_t get;
	
	struct {
		struct pn_server *server;
		struct pn_param *parent;
	} bind; // populated by pn_server_start()
} pn_param_t;

// --- Constructors ---

static inline pn_param_t pn_float(const char *name, void *ctx,
								   void (*set)(void *, float),
								   float (*get)(void *),
								   float min, float max, float step) {
	return (pn_param_t){
		.node = { .name = name, .type = PN_FLOAT,
				  .meta.float_meta = { min, max, step } },
		.ctx = ctx, .set.f = set, .get.f = get,
	};
}

static inline pn_param_t pn_int(const char *name, void *ctx,
								 void (*set)(void *, int32_t),
								 int32_t (*get)(void *),
								 int32_t min, int32_t max, int32_t step) {
	return (pn_param_t){
		.node = { .name = name, .type = PN_INT,
				  .meta.int_meta = { min, max, step } },
		.ctx = ctx, .set.i = set, .get.i = get,
	};
}

static inline pn_param_t pn_bool(const char *name, void *ctx,
								  void (*set)(void *, bool),
								  bool (*get)(void *)) {
	return (pn_param_t){
		.node = { .name = name, .type = PN_BOOL },
		.ctx = ctx, .set.b = set, .get.b = get,
	};
}

static inline pn_param_t pn_enum(const char *name, void *ctx,
								  void (*set)(void *, uint32_t),
								  uint32_t (*get)(void *),
								  const char *const *labels,
								  uint32_t count) {
	return (pn_param_t){
		.node = { .name = name, .type = PN_ENUM,
				  .meta.enum_meta = { labels, count } },
		.ctx = ctx, .set.e = set, .get.e = get,
	};
}

static inline pn_param_t pn_string(const char *name, void *ctx,
									void (*set)(void *, const char *),
									const char *(*get)(void *),
									uint32_t max_len) {
	return (pn_param_t){
		.node = { .name = name, .type = PN_STRING,
				  .meta.string_meta = { max_len } },
		.ctx = ctx, .set.s = set, .get.s = get,
	};
}

static inline pn_param_t pn_action(const char *name, void *ctx,
									void (*action)(void *)) {
	return (pn_param_t){
		.node = { .name = name, .type = PN_ACTION },
		.ctx = ctx, .set.action = action,
	};
}

static inline pn_param_t pn_group(const char *name,
								   pn_param_t *children,
								   uint32_t num_children) {
	return (pn_param_t){
		.node = { .name = name, .type = PN_NONE },
		.children = children,
		.num_children = num_children,
	};
}

// --- Server (opaque) ---

typedef struct pn_server pn_server_t;

typedef struct {
	pn_param_t *root;
	int (*send)(const uint8_t *buf, uint32_t len, void *ctx);
	void *send_ctx;
} pn_server_config_t;

// --- Server API ---

uint32_t pn_server_alloc_size(const pn_server_config_t *config);
void   pn_server_init(pn_server_t *server, const pn_server_config_t *config);
int    pn_server_start(pn_server_t *server);
int    pn_server_feed(pn_server_t *server, const uint8_t *data, uint32_t len);
void   pn_server_print_tree(const pn_server_t *server);

// --- Param API ---

int    pn_param_notify(pn_param_t *param);

#ifdef __cplusplus
}
#endif

#endif /* pn_server_h */
