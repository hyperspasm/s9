#ifndef pn_param_h
#define pn_param_h

#include <pn/pn_server.h>

// --- Internal param operations ---
// These are used by pn_server dispatch and are not part of the public API.

int pn_param_set(const pn_param_t *param, const pn_value_t *val);
int pn_param_get(const pn_param_t *param, pn_value_t *val);

const pn_param_t *pn_param_find(const pn_param_t *root, const char *path);

typedef int (*pn_param_visitor_t)(const pn_param_t *param,
								  const pn_param_t **path,
								  uint32_t depth,
								  void *ctx);

int pn_param_walk(const pn_param_t *root, pn_param_visitor_t visit, void *ctx,
				  const pn_param_t **path, uint32_t max_depth);

#endif /* pn_param_h */
