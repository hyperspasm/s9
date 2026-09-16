#include "pn_cobs.h"
#include "pn_crc.h"
#include <string.h>

struct pn_cobs {
	pn_cobs_config_t config;
	uint32_t rx_buf_size;
	uint32_t rx_pos;
	uint8_t rx_buf[];
};

// Max COBS-encoded frame size (excluding delimiter)
static uint32_t
max_frame_size(uint32_t max_pkt) {
	uint32_t with_crc = max_pkt + 2;
	return with_crc + (with_crc + 253) / 254;
}

uint32_t
pn_cobs_alloc_size(const pn_cobs_config_t *config) {
	return (uint32_t)(sizeof(struct pn_cobs) + max_frame_size(config->max_packet_size));
}

int
pn_cobs_init(pn_cobs_t *cobs, const pn_cobs_config_t *config) {
	uint32_t buf_size = max_frame_size(config->max_packet_size);
	memset(cobs, 0, sizeof(*cobs) + buf_size);
	cobs->config = *config;
	cobs->rx_buf_size = buf_size;
	return 0;
}

// --- COBS encode ---

// Encodes data + CRC-16 (little-endian) into dst using COBS.
// Returns the number of bytes written (excluding delimiter).
static uint32_t
cobs_encode(const uint8_t *data, uint32_t data_len, uint16_t crc, uint8_t *dst) {
	uint8_t *dst_start = dst;
	uint8_t *code_ptr = dst++;
	uint8_t code = 1;
	uint32_t total = data_len + 2;

	for (uint32_t i = 0; i < total; i++) {
		uint8_t byte;
		if (i < data_len)
			byte = data[i];
		else if (i == data_len)
			byte = (uint8_t)(crc & 0xFF);
		else
			byte = (uint8_t)(crc >> 8);

		if (byte == 0) {
			*code_ptr = code;
			code_ptr = dst++;
			code = 1;
		} else {
			*dst++ = byte;
			code++;
			if (code == 0xFF) {
				*code_ptr = code;
				code_ptr = dst++;
				code = 1;
			}
		}
	}
	*code_ptr = code;

	return (uint32_t)(dst - dst_start);
}

int
pn_cobs_encode(pn_cobs_t *cobs, const uint8_t *data, uint32_t len,
			   uint8_t *out, uint32_t *out_len) {
	if (len > cobs->config.max_packet_size)
		return -1;

	uint32_t need = PN_COBS_MAX_ENCODED_SIZE(len);
	if (*out_len < need)
		return -1;

	uint16_t crc = pn_crc16(data, len);
	uint32_t enc_len = cobs_encode(data, len, crc, out);
	out[enc_len++] = 0x00;

	*out_len = enc_len;
	return 0;
}

// --- COBS decode ---

// Decodes in-place. Output is always <= input size.
// Returns 0 on success, -1 on invalid framing.
static int
cobs_decode(uint8_t *buf, uint32_t len, uint32_t *out_len) {
	const uint8_t *src = buf;
	const uint8_t *src_end = buf + len;
	uint8_t *dst = buf;

	while (src < src_end) {
		uint8_t code = *src++;
		if (code == 0)
			return -1;

		uint8_t count = code - 1;
		if (src + count > src_end)
			return -1;

		for (uint8_t i = 0; i < count; i++)
			*dst++ = *src++;

		if (code < 0xFF && src < src_end)
			*dst++ = 0;
	}

	*out_len = (uint32_t)(dst - buf);
	return 0;
}

// --- Feed ---

int
pn_cobs_feed(pn_cobs_t *cobs, const uint8_t *data, uint32_t len) {
	for (uint32_t i = 0; i < len; i++) {
		uint8_t byte = data[i];

		if (byte == 0x00) {
			if (cobs->rx_pos > 0 && cobs->rx_pos <= cobs->rx_buf_size) {
				uint32_t decoded_len;
				if (cobs_decode(cobs->rx_buf, cobs->rx_pos, &decoded_len) == 0
					&& decoded_len >= 2) {
					uint32_t payload_len = decoded_len - 2;
					uint16_t rx_crc = (uint16_t)cobs->rx_buf[payload_len]
									| ((uint16_t)cobs->rx_buf[payload_len + 1] << 8);
					uint16_t calc_crc = pn_crc16(cobs->rx_buf, payload_len);
					if (rx_crc == calc_crc && cobs->config.on_packet)
						cobs->config.on_packet(cobs->rx_buf, payload_len,
											   cobs->config.ctx);
				}
			}
			cobs->rx_pos = 0;
		} else {
			if (cobs->rx_pos < cobs->rx_buf_size)
				cobs->rx_buf[cobs->rx_pos] = byte;
			// Always advance so overflow is detectable (rx_pos > rx_buf_size)
			cobs->rx_pos++;
		}
	}

	return 0;
}
