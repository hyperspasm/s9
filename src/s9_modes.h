#ifndef s9_modes_h
#define s9_modes_h

// The display modes. A mode is a set of components and their layout; the
// component registry maps an id to that set. Ids are in stepping order.

typedef enum {
	S9_MODE_FULL,
	S9_MODE_SCOPE,
	S9_MODE_SPECTRUM,
	S9_MODE_COUNT,
} s9_mode_id_t;

static const char *const s9_mode_names[S9_MODE_COUNT] = {
	[S9_MODE_FULL]     = "full",
	[S9_MODE_SCOPE]    = "scope",
	[S9_MODE_SPECTRUM] = "spectrum",
};

#endif /* s9_modes_h */
