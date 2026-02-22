#ifndef pn_crc_h
#define pn_crc_h

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t pn_crc16(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* pn_crc_h */
