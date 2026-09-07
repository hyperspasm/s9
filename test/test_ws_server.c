// Loopback websocket hub for test_ws.py: every message received is sent to
// every client, the manifest is a fixed text, and the web files are the ones
// built into the binary, or a directory given as the second argument.
//
// A message of exactly {0xff, lo, hi, 0x00} is a burst command instead: the
// server sends (hi << 8 | lo) messages of the largest size the hub carries,
// each numbered in its first two bytes, so a test can produce more replies
// than one client could ask for.
//
//   test_ws_server PORT [WWW_DIR]
#include "s9_ws.h"
#include "s9.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char manifest[] = "# pn manifest v1 test fnv1a32=811c9dc5\n";
static s9_ws_t *hub;

static void echo(const uint8_t *data, uint32_t len, void *ctx) {
	(void)ctx;
	if (len == 4 && data[0] == 0xff) {
		static uint8_t big[1024];
		unsigned n = data[1] | (unsigned)data[2] << 8;
		for (unsigned i = 0; i < n; i++) {
			big[0] = (uint8_t)i;
			big[1] = (uint8_t)(i >> 8);
			big[sizeof(big) - 1] = 0;
			s9_ws_send(big, sizeof(big), hub);
		}
		return;
	}
	s9_ws_send(data, len, hub);
}

int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: test_ws_server PORT [WWW_DIR]\n"); return 2; }

	hub = s9_ws_new(&(s9_ws_config_t){
		.port = (uint16_t)atoi(argv[1]),
		.www_dir = argc > 2 ? argv[2] : NULL,
		.manifest = manifest,
		.manifest_len = sizeof(manifest) - 1,
		.on_recv = echo,
	});
	if (!hub) return 1;

	struct timespec ts = { 0, 2 * 1000 * 1000 };
	for (;;) {
		s9_ws_update(hub);
		nanosleep(&ts, NULL);
	}
}
