#include "pn_fnv.h"

#define FNV1A32_PRIME 0x01000193u

uint32_t
pn_fnv1a32(uint32_t hash, const uint8_t *data, uint32_t len) {
	for (uint32_t i = 0; i < len; i++) {
		hash ^= data[i];
		hash *= FNV1A32_PRIME;
	}
	return hash;
}
