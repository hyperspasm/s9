#ifndef amp_tree_h
#define amp_tree_h

#include <pn/pn.h>

// The example schema from MANIFEST.md: root "amp", every type, one nested group.
// Built without bindings: every callback and ctx is NULL.

typedef struct {
	pn_param_t eq[2];
	pn_param_t nodes[6];
	pn_param_t root;
} amp_tree_t;

pn_param_t *amp_tree_build(amp_tree_t *t);

#endif /* amp_tree_h */
