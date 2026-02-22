#ifndef pn_client_h
#define pn_client_h

#include "pn_node.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Client (opaque) ---

typedef struct pn_client pn_client_t;

// --- Callback ---
// Arguments are only valid for the duration of the call. Copy strings if needed.

typedef void (*pn_on_update_t)(const char *path,
							   const pn_value_t *val,
							   pn_type_t type, void *ctx);

// --- Config ---

typedef struct {
	int (*send)(const uint8_t *buf, uint32_t len, void *ctx);
	void *send_ctx;
	pn_on_update_t on_update;
	void *on_update_ctx;
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

#ifdef __cplusplus
}
#endif

#endif /* pn_client_h */
