#include "amp_tree.h"

static const char *const input_labels[] = { "mic", "line", "aux" };

pn_param_t *amp_tree_build(amp_tree_t *t) {
	t->eq[0]    = pn_float("bass",   NULL, NULL, NULL, -12.0f, 12.0f, 0.5f);
	t->eq[1]    = pn_float("treble", NULL, NULL, NULL, -12.0f, 12.0f, 0.5f);
	t->nodes[0] = pn_bool("power",   NULL, NULL, NULL);
	t->nodes[1] = pn_int("volume",   NULL, NULL, NULL, 0, 100, 1);
	t->nodes[2] = pn_group("eq", t->eq, 2);
	t->nodes[3] = pn_enum("input",   NULL, NULL, NULL, input_labels, 3);
	t->nodes[4] = pn_string("name",  NULL, NULL, NULL, 32);
	t->nodes[5] = pn_action("reset", NULL, NULL);
	t->root     = pn_group("amp", t->nodes, 6);
	return &t->root;
}
