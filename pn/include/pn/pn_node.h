#ifndef pn_node_h
#define pn_node_h

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Limits ---

// Wire format limits — must match pn.options. Verified at compile time in pn_server.c.
#define PN_MAX_PATH   256
#define PN_MAX_STRING 256

// Maximum parameter tree depth (root = depth 0)
#define PN_MAX_DEPTH  16

// --- Types ---

typedef enum {
	PN_NONE,
	PN_ACTION,
	PN_BOOL,
	PN_INT,
	PN_FLOAT,
	PN_ENUM,
	PN_STRING,
} pn_type_t;

typedef union {
	bool b;
	int32_t i;
	float f;
	uint32_t e;
	const char *s;
} pn_value_t;

// --- Type metadata ---

typedef struct {
	int32_t min;
	int32_t max;
	int32_t step;
} pn_int_meta_t;

typedef struct {
	float min;
	float max;
	float step;
} pn_float_meta_t;

typedef struct {
	const char *const *labels;
	uint32_t count;
} pn_enum_meta_t;

typedef struct {
	uint32_t max_len;
} pn_string_meta_t;

typedef union {
	pn_int_meta_t int_meta;
	pn_float_meta_t float_meta;
	pn_enum_meta_t enum_meta;
	pn_string_meta_t string_meta;
} pn_meta_t;

// --- Node ---

typedef struct pn_node {
	const char *name;
	pn_type_t type;
	pn_meta_t meta;
	bool readonly;    // a client's set is refused
} pn_node_t;

#ifdef __cplusplus
}
#endif

#endif /* pn_node_h */
