#ifndef s9_params_h
#define s9_params_h

#include <pn/pn.h>

// The s9 parameter tree: storage for its nodes and the function that builds
// them. Callbacks are referenced through PN_FN(), so compiled with
// PN_SCHEMA_ONLY this module depends on nothing but pn.

#define S9_OSC_NUM_PARAMS   7
#define S9_HARM_NUM_PARAMS  14
#define S9_PITCH_NUM_PARAMS 2
#define S9_DEBUG_NUM_PARAMS 1

// The root's children, in declared order
typedef enum {
	S9_ROOT_MODE,
	S9_ROOT_OSC,
	S9_ROOT_HARM,
	S9_ROOT_PITCH,
	S9_ROOT_DEBUG,
	S9_ROOT_COUNT,
} s9_root_child_t;

// The "pitch" group's children, in declared order
typedef enum {
	S9_PITCH_FREQUENCY,
	S9_PITCH_VALID,
} s9_pitch_param_t;

typedef struct {
	pn_param_t osc_params[S9_OSC_NUM_PARAMS];
	pn_param_t harm_params[S9_HARM_NUM_PARAMS];
	pn_param_t pitch_params[S9_PITCH_NUM_PARAMS];  // indexed by s9_pitch_param_t
	pn_param_t debug_params[S9_DEBUG_NUM_PARAMS];
	pn_param_t root_children[S9_ROOT_COUNT];  // indexed by s9_root_child_t
	pn_param_t root;
} s9_params_t;

// Build the tree into p and return its root. osc is the oscilloscope the
// "osc" parameters read and write; harm is the harmonics component the
// "harm" parameters read and write and the "pitch" parameters read. Both
// are NULL in a schema-only build. The mode and debug parameters read and
// write the app.
pn_param_t *s9_params_build(s9_params_t *p, void *osc, void *harm);

#endif /* s9_params_h */
