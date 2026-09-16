#ifndef pn_fnv_h
#define pn_fnv_h

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// FNV-1a, 32-bit. Incremental: pass PN_FNV1A32_INIT for the first call and the
// previous result for each subsequent call.

#define PN_FNV1A32_INIT 0x811c9dc5u

uint32_t pn_fnv1a32(uint32_t hash, const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* pn_fnv_h */
