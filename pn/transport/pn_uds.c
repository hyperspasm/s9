#include "pn_uds.h"

#ifdef PN_HAS_UDS

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

struct pn_uds {
	pn_uds_config_t config;
	int listen_fd;
	int fd;
	uint32_t read_buf_size;
	uint8_t read_buf[];
};

static int
set_nonblocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1) return -1;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

uint32_t
pn_uds_alloc_size(const pn_uds_config_t *config) {
	return (uint32_t)(sizeof(struct pn_uds) + config->read_buf_size);
}

int
pn_uds_init(pn_uds_t *uds, const pn_uds_config_t *config) {
	memset(uds, 0, sizeof(struct pn_uds) + config->read_buf_size);
	uds->config = *config;
	uds->listen_fd = -1;
	uds->fd = -1;
	uds->read_buf_size = config->read_buf_size;
	return 0;
}

int
pn_uds_listen(pn_uds_t *uds) {
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) return -1;

	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	strncpy(addr.sun_path, uds->config.path, sizeof(addr.sun_path) - 1);

	unlink(uds->config.path);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		close(fd);
		return -1;
	}

	if (listen(fd, 1) == -1) {
		close(fd);
		return -1;
	}

	if (set_nonblocking(fd) == -1) {
		close(fd);
		return -1;
	}

	uds->listen_fd = fd;
	return 0;
}

int
pn_uds_connect(pn_uds_t *uds) {
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) return -1;

	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	strncpy(addr.sun_path, uds->config.path, sizeof(addr.sun_path) - 1);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		close(fd);
		return -1;
	}

	if (set_nonblocking(fd) == -1) {
		close(fd);
		return -1;
	}

	uds->fd = fd;
	return 0;
}

int
pn_uds_poll(pn_uds_t *uds) {
	// Server: accept new connection if none
	if (uds->listen_fd != -1 && uds->fd == -1) {
		int fd = accept(uds->listen_fd, NULL, NULL);
		if (fd != -1) {
			set_nonblocking(fd);
			uds->fd = fd;
		}
	}

	if (uds->fd == -1)
		return 0;

	ssize_t n = read(uds->fd, uds->read_buf, uds->read_buf_size);

	if (n > 0) {
		if (uds->config.on_recv)
			uds->config.on_recv(uds->read_buf, (uint32_t)n, uds->config.on_recv_ctx);
		return (int)n;
	}

	if (n == 0) {
		// Peer disconnected
		close(uds->fd);
		uds->fd = -1;
		return 0;
	}

	// n == -1
	if (errno == EAGAIN || errno == EWOULDBLOCK)
		return 0;

	// Actual error
	close(uds->fd);
	uds->fd = -1;
	return -1;
}

int
pn_uds_send(const uint8_t *buf, uint32_t len, void *ctx) {
	pn_uds_t *uds = (pn_uds_t *)ctx;
	if (uds->fd == -1) return -1;

	uint32_t sent = 0;
	while (sent < len) {
		ssize_t n = write(uds->fd, buf + sent, len - sent);
		if (n <= 0) return -1;
		sent += (uint32_t)n;
	}
	return 0;
}

void
pn_uds_close(pn_uds_t *uds) {
	if (uds->fd != -1) {
		close(uds->fd);
		uds->fd = -1;
	}
	if (uds->listen_fd != -1) {
		close(uds->listen_fd);
		unlink(uds->config.path);
		uds->listen_fd = -1;
	}
}

#else /* !PN_HAS_UDS */

uint32_t pn_uds_alloc_size(const pn_uds_config_t *config) { (void)config; return 1; }
int      pn_uds_init(pn_uds_t *uds, const pn_uds_config_t *config) { (void)uds; (void)config; return -1; }
int      pn_uds_listen(pn_uds_t *uds) { (void)uds; return -1; }
int      pn_uds_connect(pn_uds_t *uds) { (void)uds; return -1; }
int      pn_uds_poll(pn_uds_t *uds) { (void)uds; return -1; }
int      pn_uds_send(const uint8_t *buf, uint32_t len, void *ctx) { (void)buf; (void)len; (void)ctx; return -1; }
void     pn_uds_close(pn_uds_t *uds) { (void)uds; }

#endif /* PN_HAS_UDS */
