// Host schema tool: writes the mixer's manifest to stdout.
// Built with PN_SCHEMA_ONLY, so mixer.c contributes only its tree.

#include <pn/pn.h>
#include "mixer.h"
#include <stdio.h>

static int write_stdout(const uint8_t *buf, uint32_t len, void *ctx) {
	(void)ctx;
	return fwrite(buf, 1, len, stdout) == len ? 0 : -1;
}

int main(void) {
	mixer_t mixer = {0};
	pn_param_t *root = mixer_build_tree(&mixer);

	const pn_param_t *offender = NULL;
	pn_schema_result_t result = pn_schema_validate(root, &offender);
	if (result != PN_SCHEMA_OK) {
		fprintf(stderr, "invalid schema: %s (%s)\n",
				pn_schema_result_name(result),
				offender && offender->node.name ? offender->node.name : "-");
		return 1;
	}

	return pn_manifest_write(root, write_stdout, NULL) == 0 ? 0 : 1;
}
