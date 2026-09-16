#include <pn/pn.h>
#include <pn_uds.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdalign.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/select.h>

#define SOCKET_PATH  "/tmp/pn_example.sock"
#define READ_BUF     1024
#define POLL_MS      30
#define LINE_BUF     512

static volatile sig_atomic_t running = 1;

static void on_signal(int sig) {
	(void)sig;
	running = 0;
}

// Feed bytes from UDS into the client
static void on_recv(const uint8_t *data, uint32_t len, void *ctx) {
	pn_client_t *client = (pn_client_t *)ctx;
	pn_client_feed(client, data, len);
}

// Print updates from the server
static void on_update(const char *path, const pn_value_t *val,
					  pn_type_t type, void *ctx) {
	(void)ctx;
	switch (type) {
	case PN_BOOL:   printf("  %s = %s\n", path, val->b ? "true" : "false"); break;
	case PN_INT:    printf("  %s = %d\n", path, val->i); break;
	case PN_FLOAT:  printf("  %s = %.4g\n", path, (double)val->f); break;
	case PN_ENUM:   printf("  %s = %u\n", path, val->e); break;
	case PN_STRING: printf("  %s = \"%s\"\n", path, val->s); break;
	default: break;
	}
	fflush(stdout);
}

// Parse and send a command line
static void handle_line(pn_client_t *client, char *line) {
	// Trim trailing whitespace/newline
	uint32_t len = (uint32_t)strlen(line);
	while (len > 0 && isspace((unsigned char)line[len - 1]))
		line[--len] = '\0';

	if (len == 0)
		return;

	// Action: !path
	if (line[0] == '!') {
		pn_client_action(client, line + 1);
		return;
	}

	// Split into path and optional value
	char *space = strchr(line, ' ');
	if (!space) {
		// Get: just a path
		pn_client_get(client, line);
		return;
	}

	*space = '\0';
	const char *path = line;
	const char *val_str = space + 1;

	// Skip whitespace after path
	while (*val_str && isspace((unsigned char)*val_str))
		val_str++;

	// Bool
	if (strcmp(val_str, "true") == 0) {
		pn_client_set_bool(client, path, true);
		return;
	}
	if (strcmp(val_str, "false") == 0) {
		pn_client_set_bool(client, path, false);
		return;
	}

	// Float (contains '.')
	if (strchr(val_str, '.')) {
		pn_client_set_float(client, path, strtof(val_str, NULL));
		return;
	}

	// Int
	pn_client_set_int(client, path, (int32_t)strtol(val_str, NULL, 10));
}

static void print_usage(void) {
	printf("commands:\n");
	printf("  path              get value\n");
	printf("  path value        set value (auto-detects type)\n");
	printf("  !path             trigger action\n");
	printf("  quit              exit\n\n");
}

int main(void) {
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	// Allocate client
	pn_client_config_t client_config = {
		.send = pn_uds_send,
		.on_update = on_update,
	};
	uint32_t client_size = pn_client_alloc_size(&client_config);
	alignas(max_align_t) uint8_t client_mem[client_size];
	pn_client_t *client = (pn_client_t *)client_mem;

	// Create UDS transport
	pn_uds_config_t uds_config = {
		.path = SOCKET_PATH,
		.read_buf_size = READ_BUF,
		.on_recv = on_recv,
		.on_recv_ctx = client,
	};
	uint32_t uds_size = pn_uds_alloc_size(&uds_config);
	alignas(max_align_t) uint8_t uds_mem[uds_size];
	pn_uds_t *uds = (pn_uds_t *)uds_mem;
	pn_uds_init(uds, &uds_config);

	// Init client with UDS as send context
	client_config.send_ctx = uds;
	pn_client_init(client, &client_config);

	// Connect to server
	if (pn_uds_connect(uds) != 0) {
		fprintf(stderr, "failed to connect to %s\n", SOCKET_PATH);
		return 1;
	}
	printf("connected to %s\n", SOCKET_PATH);
	print_usage();

	// Read-eval-print loop
	char line[LINE_BUF];
	printf("> ");
	fflush(stdout);

	while (running) {
		fd_set fds;
		struct timeval tv = { .tv_sec = 0, .tv_usec = POLL_MS * 1000 };
		FD_ZERO(&fds);
		FD_SET(STDIN_FILENO, &fds);

		int ready = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
		if (ready < 0)
			break;

		// Always poll for server data
		pn_uds_poll(uds);

		// Stdin ready
		if (ready > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
			if (!fgets(line, sizeof(line), stdin))
				break;

			if (strncmp(line, "quit", 4) == 0)
				break;

			handle_line(client, line);
			printf("> ");
			fflush(stdout);
		}
	}

	printf("\ndisconnecting\n");
	pn_uds_close(uds);
	return 0;
}
