#ifndef s9_params_h
#define s9_params_h

#include <pn/pn.h>

// The s9 parameter tree: storage for its nodes and the function that builds
// them. Callbacks are referenced through PN_FN(), so compiled with
// PN_SCHEMA_ONLY this module depends on nothing but pn.

#define S9_OSC_NUM_PARAMS 7

typedef struct {
	pn_param_t osc_params[S9_OSC_NUM_PARAMS];
	pn_param_t osc_group;
	pn_param_t root;
} s9_params_t;

// Build the tree into p and return its root. osc is the oscilloscope the
// "osc" parameters read and write, or NULL in a schema-only build.
pn_param_t *s9_params_build(s9_params_t *p, void *osc);

#endif /* s9_params_h */
