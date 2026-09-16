#ifndef pn_client_h
#define pn_client_h

#include "pn_node.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Client (opaque) ---

typedef struct pn_client pn_client_t;

// --- Callbacks ---
// Arguments are only valid for the duration of the call. Copy what you keep.

// An Update: a parameter's current value.
typedef void (*pn_on_update_t)(const char *path,
							   const pn_value_t *val,
							   pn_type_t type, void *ctx);

// A server's reply to pn_client_get_schema(): its schema hash and root name.
typedef void (*pn_on_schema_t)(uint32_t hash, const char *root, void *ctx);

// One chunk of a server's reply to pn_client_get_manifest(). Chunks arrive in
// order and last is set on the final one. Chunk boundaries carry no meaning:
// the manifest is the concatenation of the data.
typedef void (*pn_on_manifest_t)(const uint8_t *data, uint32_t len,
								 bool last, void *ctx);

// --- Config ---

typedef struct {
	int (*send)(const uint8_t *buf, uint32_t len, void *ctx);
	void *send_ctx;
	pn_on_update_t on_update;
	void *on_update_ctx;
	pn_on_schema_t on_schema;
	void *on_schema_ctx;
	pn_on_manifest_t on_manifest;
	void *on_manifest_ctx;
} pn_client_config_t;

// --- Lifecycle ---

uint32_t pn_client_alloc_size(const pn_client_config_t *config);
void   pn_client_init(pn_client_t *client, const pn_client_config_t *config);
int    pn_client_feed(pn_client_t *client, const uint8_t *data, uint32_t len);

// --- Set ---

int pn_client_set_float(pn_client_t *client, const char *path, float val);
int pn_client_set_int(pn_client_t *client, const char *path, int32_t val);
int pn_client_set_bool(pn_client_t *client, const char *path, bool val);
int pn_client_set_enum(pn_client_t *client, const char *path, uint32_t val);
int pn_client_set_string(pn_client_t *client, const char *path, const char *val);

// --- Get / Action ---

int pn_client_get(pn_client_t *client, const char *path);
int pn_client_action(pn_client_t *client, const char *path);

// --- Schema / Manifest ---

// Ask for a schema hash. root names the server; NULL or "" asks every server
// on the transport, and each replies with its own root.
int pn_client_get_schema(pn_client_t *client, const char *root);

// Ask the server with this root for its manifest, delivered through on_manifest.
int pn_client_get_manifest(pn_client_t *client, const char *root);

#ifdef __cplusplus
}
#endif

#endif /* pn_client_h */
