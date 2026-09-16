#ifndef s9_params_h
#define s9_params_h

#include <pn/pn.h>

// The s9 parameter tree: storage for its nodes and the function that builds
// them. Callbacks are referenced through PN_FN(), so compiled with
// PN_SCHEMA_ONLY this module depends on nothing but pn.

#define S9_OSC_NUM_PARAMS                7
#define S9_SPECTRUM_NUM_PARAMS           6
#define S9_PERIODIC_NUM_PARAMS             6   // channel, view, and the fundamental, harmonics, waveform, and note groups
#define S9_PERIODIC_FUNDAMENTAL_NUM_PARAMS 16
#define S9_PERIODIC_HARMONICS_NUM_PARAMS   4
#define S9_PERIODIC_WAVEFORM_NUM_PARAMS    4
#define S9_PERIODIC_NOTE_NUM_PARAMS        1
#define S9_SPECTROGRAM_NUM_PARAMS        11
#define S9_DEBUG_NUM_PARAMS              1

// The root's children, in declared order
typedef enum {
	S9_ROOT_MODE,
	S9_ROOT_OSC,
	S9_ROOT_SPECTRUM,
	S9_ROOT_PERIODIC,
	S9_ROOT_SPECTROGRAM,
	S9_ROOT_DEBUG,
	S9_ROOT_COUNT,
} s9_root_child_t;

// The periodic analyzer's "fundamental" group's children that the app pushes, in declared order
typedef enum {
	S9_FUNDAMENTAL_FREQUENCY,
	S9_FUNDAMENTAL_VALID,
	S9_FUNDAMENTAL_MIDI,
} s9_fundamental_param_t;

// The spectrogram group's children that the app pushes, in declared order
typedef enum {
	S9_SPECTROGRAM_TIME_RESOLUTION,
	S9_SPECTROGRAM_FLOOR_FREQUENCY,
} s9_spectrogram_param_t;

typedef struct {
	pn_param_t osc_params[S9_OSC_NUM_PARAMS];
	pn_param_t spectrum_params[S9_SPECTRUM_NUM_PARAMS];
	pn_param_t periodic_fundamental_params[S9_PERIODIC_FUNDAMENTAL_NUM_PARAMS];   // indexed by s9_fundamental_param_t
	pn_param_t periodic_harmonics_params[S9_PERIODIC_HARMONICS_NUM_PARAMS];
	pn_param_t periodic_waveform_params[S9_PERIODIC_WAVEFORM_NUM_PARAMS];
	pn_param_t periodic_note_params[S9_PERIODIC_NOTE_NUM_PARAMS];
	pn_param_t periodic_params[S9_PERIODIC_NUM_PARAMS];
	pn_param_t spectrogram_params[S9_SPECTROGRAM_NUM_PARAMS];   // indexed by s9_spectrogram_param_t
	pn_param_t debug_params[S9_DEBUG_NUM_PARAMS];
	pn_param_t root_children[S9_ROOT_COUNT];  // indexed by s9_root_child_t
	pn_param_t root;
} s9_params_t;

// Build the tree into p and return its root. osc, spectrum, periodic and
// spectrogram are the oscilloscope, the stereo spectrum, the periodic
// analyzer and the spectrogram that the "osc", "spectrum", "periodic" and
// "spectrogram" parameters read and write; all NULL in a schema-only build. The mode and
// debug parameters read and write the app.
pn_param_t *s9_params_build(s9_params_t *p, void *osc, void *spectrum, void *periodic, void *spectrogram);

#endif /* s9_params_h */
