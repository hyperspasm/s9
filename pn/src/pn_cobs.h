#ifndef pn_cobs_h
#define pn_cobs_h

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pn_cobs pn_cobs_t;

typedef struct {
	uint32_t max_packet_size;
	void (*on_packet)(const uint8_t *data, uint32_t len, void *ctx);
	void *ctx;
} pn_cobs_config_t;

// Max encoded size for a given max payload (CRC + COBS overhead + delimiter)
#define PN_COBS_MAX_ENCODED_SIZE(n) \
	((n) + 2 + (((n) + 2 + 253) / 254) + 1)

uint32_t pn_cobs_alloc_size(const pn_cobs_config_t *config);
int      pn_cobs_init(pn_cobs_t *cobs, const pn_cobs_config_t *config);

// Encode: computes CRC-16, COBS-encodes, appends 0x00 delimiter.
// *out_len: in = buffer capacity, out = encoded length.
int      pn_cobs_encode(pn_cobs_t *cobs, const uint8_t *data, uint32_t len,
						uint8_t *out, uint32_t *out_len);

// Feed received bytes. Calls on_packet for each complete, CRC-verified frame.
int      pn_cobs_feed(pn_cobs_t *cobs, const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* pn_cobs_h */
