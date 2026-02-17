#include <pn/pn.h>
#include <pn_uds.h>
#include "mixer.h"
#include <stdio.h>
#include <stdint.h>
#include <stdalign.h>
#include <signal.h>
#include <time.h>

#define SOCKET_PATH  "/tmp/pn_example.sock"
#define READ_BUF     1024
#define POLL_MS      30

static volatile sig_atomic_t running = 1;

static void on_signal(int sig) {
	(void)sig;
	running = 0;
}

static void sleep_ms(int ms) {
	struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

// Feed bytes from UDS into the server
static void on_recv(const uint8_t *data, uint32_t len, void *ctx) {
	pn_server_t *server = (pn_server_t *)ctx;
	pn_server_feed(server, data, len);
}

int main(void) {
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	// Init the mixer and build the parameter tree
	mixer_t mixer;
	mixer_init(&mixer);
	pn_param_t *root = mixer_build_tree(&mixer);

	// Allocate server memory
	pn_server_config_t server_config = {
		.root = root,
		.send = pn_uds_send,
	};
	uint32_t server_size = pn_server_alloc_size(&server_config);
	alignas(max_align_t) uint8_t server_mem[server_size];
	pn_server_t *server = (pn_server_t *)server_mem;

	// Create UDS transport (on_recv only called during poll, so server pointer is safe)
	pn_uds_config_t uds_config = {
		.path = SOCKET_PATH,
		.read_buf_size = READ_BUF,
		.on_recv = on_recv,
		.on_recv_ctx = server,
	};
	uint32_t uds_size = pn_uds_alloc_size(&uds_config);
	alignas(max_align_t) uint8_t uds_mem[uds_size];
	pn_uds_t *uds = (pn_uds_t *)uds_mem;
	pn_uds_init(uds, &uds_config);

	// Init and start the server
	server_config.send_ctx = uds;
	pn_server_init(server, &server_config);
	pn_server_start(server);

	printf("server allocated %u bytes\n", server_size);
	printf("mixer initialized with %d channels\n", NUM_CHANNELS);

	// Print the parameter tree
	pn_server_print_tree(server);

	// Listen for connections
	if (pn_uds_listen(uds) != 0) {
		fprintf(stderr, "failed to listen on %s\n", SOCKET_PATH);
		return 1;
	}
	printf("listening on %s\n", SOCKET_PATH);

	// Poll loop
	while (running) {
		pn_uds_poll(uds);
		sleep_ms(POLL_MS);
	}

	printf("\nshutting down\n");
	pn_uds_close(uds);
	return 0;
}
