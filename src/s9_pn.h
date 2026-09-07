#ifndef s9_pn_h
#define s9_pn_h

#include "s9_components.h"

typedef struct s9_pn s9_pn_t;

typedef struct {
	s9_components_t *components;
} s9_pn_config_t;

s9_pn_t *s9_pn_new(const s9_pn_config_t *config);
void s9_pn_free(s9_pn_t **pn);
void s9_pn_update(s9_pn_t *pn);

// Push the current display mode to the client. For changes the app makes
// itself; a change requested by the client is echoed without this.
void s9_pn_notify_mode(s9_pn_t *pn);

// The manifest of the served tree, as text without a terminator, and its
// length. Generated once at creation; valid for the lifetime of pn.
const char *s9_pn_manifest(const s9_pn_t *pn, uint32_t *len);

#endif /* s9_pn_h */
