#ifndef s9_params_h
#define s9_params_h

#include <pn/pn.h>

// The s9 parameter tree: storage for its nodes and the function that builds
// them. Callbacks are referenced through PN_FN(), so compiled with
// PN_SCHEMA_ONLY this module depends on nothing but pn.

#define S9_OSC_NUM_PARAMS 7

// The root's children, in declared order
typedef enum {
	S9_ROOT_MODE,
	S9_ROOT_OSC,
	S9_ROOT_COUNT,
} s9_root_child_t;

typedef struct {
	pn_param_t osc_params[S9_OSC_NUM_PARAMS];
	pn_param_t root_children[S9_ROOT_COUNT];  // indexed by s9_root_child_t
	pn_param_t root;
} s9_params_t;

// Build the tree into p and return its root. osc is the oscilloscope the
// "osc" parameters read and write, or NULL in a schema-only build. The mode
// parameter reads and writes the app's display mode.
pn_param_t *s9_params_build(s9_params_t *p, void *osc);

#endif /* s9_params_h */
