#ifndef s9_params_h
#define s9_params_h

#include <pn/pn.h>

// The s9 parameter tree: storage for its nodes and the function that builds
// them. Callbacks are referenced through PN_FN(), so compiled with
// PN_SCHEMA_ONLY this module depends on nothing but pn.

#define S9_OSC_NUM_PARAMS                7
#define S9_PERIODIC_NUM_PARAMS           4   // channel, view, and the pitch and harmonics groups
#define S9_PERIODIC_PITCH_NUM_PARAMS     14
#define S9_PERIODIC_HARMONICS_NUM_PARAMS 4
#define S9_DEBUG_NUM_PARAMS              1

// The root's children, in declared order
typedef enum {
	S9_ROOT_MODE,
	S9_ROOT_OSC,
	S9_ROOT_PERIODIC,
	S9_ROOT_DEBUG,
	S9_ROOT_COUNT,
} s9_root_child_t;

// The periodic analyzer's "pitch" group's children that the app pushes, in declared order
typedef enum {
	S9_PITCH_FREQUENCY,
	S9_PITCH_VALID,
} s9_pitch_param_t;

typedef struct {
	pn_param_t osc_params[S9_OSC_NUM_PARAMS];
	pn_param_t periodic_pitch_params[S9_PERIODIC_PITCH_NUM_PARAMS];   // indexed by s9_pitch_param_t
	pn_param_t periodic_harmonics_params[S9_PERIODIC_HARMONICS_NUM_PARAMS];
	pn_param_t periodic_params[S9_PERIODIC_NUM_PARAMS];
	pn_param_t debug_params[S9_DEBUG_NUM_PARAMS];
	pn_param_t root_children[S9_ROOT_COUNT];  // indexed by s9_root_child_t
	pn_param_t root;
} s9_params_t;

// Build the tree into p and return its root. osc and periodic are the
// oscilloscope and the periodic analyzer that the "osc" and "periodic"
// parameters read and write; both NULL in a schema-only build. The mode
// and debug parameters read and write the app.
pn_param_t *s9_params_build(s9_params_t *p, void *osc, void *periodic);

#endif /* s9_params_h */
