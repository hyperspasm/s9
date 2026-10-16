#ifndef pn_manifest_h
#define pn_manifest_h

#include "pn_server.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Schema validation ---
// The tree requirements of the manifest format (see MANIFEST.md) plus the
// structural limits of this implementation.

typedef enum {
	PN_SCHEMA_OK = 0,
	PN_SCHEMA_NO_ROOT,         // root is NULL
	PN_SCHEMA_BAD_TYPE,        // a node's type is not a pn_type_t value
	PN_SCHEMA_BAD_NAME,        // a name is NULL, empty, or contains a forbidden byte
	PN_SCHEMA_BAD_LABEL,       // an enum label is NULL, empty, or contains a forbidden byte
	PN_SCHEMA_DUPLICATE_NAME,  // two siblings share a name
	PN_SCHEMA_EMPTY_ENUM,      // an enum has no labels
	PN_SCHEMA_READONLY_ACTION, // an action is marked read-only
	PN_SCHEMA_NOT_GROUP,       // a node has children but is not a group
	PN_SCHEMA_TOO_DEEP,        // the tree has more than PN_MAX_DEPTH levels
	PN_SCHEMA_PATH_TOO_LONG,   // a path does not fit in PN_MAX_PATH with its terminator
	PN_SCHEMA_BAD_UNIT,        // a unit on a node that is not an int or a float, or one that is empty or contains a forbidden byte
} pn_schema_result_t;

// Check a tree against the requirements above. If offender is not NULL it
// receives the first offending node, or NULL when the result is PN_SCHEMA_OK or
// the offender cannot be identified (PN_SCHEMA_NO_ROOT, PN_SCHEMA_TOO_DEEP).
pn_schema_result_t pn_schema_validate(const pn_param_t *root, const pn_param_t **offender);

// Short description of a validation result, for diagnostics.
const char *pn_schema_result_name(pn_schema_result_t result);

// --- Schema hash ---

// The schema hash: FNV-1a 32-bit over the canonical manifest body.
// Returns 0 on success, -1 if the tree does not validate.
int pn_schema_hash(const pn_param_t *root, uint32_t *hash);

// --- Manifest ---

// Receives manifest bytes. Same signature as the send callbacks, so a transport
// send function can serve as a sink. Return 0 to continue; any other value
// aborts the write and is returned by pn_manifest_write().
typedef int (*pn_sink_t)(const uint8_t *buf, uint32_t len, void *ctx);

// Write the manifest: the header line followed by one entry per leaf.
// Returns 0 on success, -1 if the tree does not validate, otherwise the value
// the sink returned. Reads only names, types, constraints, and children: it
// never calls a node's setter or getter and never dereferences a node's ctx,
// so a tree built with NULL bindings can be written.
int pn_manifest_write(const pn_param_t *root, pn_sink_t sink, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* pn_manifest_h */
