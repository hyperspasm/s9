// Schema tool: writes the amp schema's manifest to stdout.

#include "amp_tree.h"
#include <stdio.h>

static int write_stdout(const uint8_t *buf, uint32_t len, void *ctx) {
	(void)ctx;
	return fwrite(buf, 1, len, stdout) == len ? 0 : -1;
}

int main(void) {
	amp_tree_t t;
	pn_param_t *root = amp_tree_build(&t);
	return pn_manifest_write(root, write_stdout, NULL) == 0 ? 0 : 1;
}
