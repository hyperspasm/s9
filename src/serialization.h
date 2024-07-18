#ifndef serialization_h
#define serialization_h

#include <stdint.h>
#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__)
#define FORCE_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline
#endif

// pack functions

/**
 * packs a 16-bit unsigned integer into a buffer in little-endian format
 * 
 * @param buffer destination buffer, must be at least 2 bytes
 * @param value 16-bit unsigned integer value to pack
 */
static FORCE_INLINE void pack_uint16_le(uint8_t *buffer, uint16_t value) {
	buffer[0] = (uint8_t)(value & 0xFF);
	buffer[1] = (uint8_t)((value >> 8) & 0xFF);
}

/**
 * packs a 16-bit unsigned integer into a buffer in big-endian format
 * 
 * @param buffer destination buffer, must be at least 2 bytes
 * @param value 16-bit unsigned integer value to pack
 */
static FORCE_INLINE void pack_uint16_be(uint8_t *buffer, uint16_t value) {
	buffer[0] = (uint8_t)((value >> 8) & 0xFF);
	buffer[1] = (uint8_t)(value & 0xFF);
}

/**
 * packs a 32-bit unsigned integer into a buffer in little-endian format
 * 
 * @param buffer destination buffer, must be at least 4 bytes
 * @param value 32-bit unsigned integer value to pack
 */
static FORCE_INLINE void pack_uint32_le(uint8_t *buffer, uint32_t value) {
	buffer[0] = (uint8_t)(value & 0xFF);
	buffer[1] = (uint8_t)((value >> 8) & 0xFF);
	buffer[2] = (uint8_t)((value >> 16) & 0xFF);
	buffer[3] = (uint8_t)((value >> 24) & 0xFF);
}

/**
 * packs a 32-bit unsigned integer into a buffer in big-endian format
 * 
 * @param buffer destination buffer, must be at least 4 bytes
 * @param value 32-bit unsigned integer value to pack
 */
static FORCE_INLINE void pack_uint32_be(uint8_t *buffer, uint32_t value) {
	buffer[0] = (uint8_t)((value >> 24) & 0xFF);
	buffer[1] = (uint8_t)((value >> 16) & 0xFF);
	buffer[2] = (uint8_t)((value >> 8) & 0xFF);
	buffer[3] = (uint8_t)(value & 0xFF);
}

/**
 * packs a 64-bit unsigned integer into a buffer in little-endian format
 * 
 * @param buffer destination buffer, must be at least 8 bytes
 * @param value 64-bit unsigned integer value to pack
 */
static FORCE_INLINE void pack_uint64_le(uint8_t *buffer, uint64_t value) {
	buffer[0] = (uint8_t)(value & 0xFF);
	buffer[1] = (uint8_t)((value >> 8) & 0xFF);
	buffer[2] = (uint8_t)((value >> 16) & 0xFF);
	buffer[3] = (uint8_t)((value >> 24) & 0xFF);
	buffer[4] = (uint8_t)((value >> 32) & 0xFF);
	buffer[5] = (uint8_t)((value >> 40) & 0xFF);
	buffer[6] = (uint8_t)((value >> 48) & 0xFF);
	buffer[7] = (uint8_t)((value >> 56) & 0xFF);
}

/**
 * packs a 64-bit unsigned integer into a buffer in big-endian format
 * 
 * @param buffer destination buffer, must be at least 8 bytes
 * @param value 64-bit unsigned integer value to pack
 */
static FORCE_INLINE void pack_uint64_be(uint8_t *buffer, uint64_t value) {
	buffer[0] = (uint8_t)((value >> 56) & 0xFF);
	buffer[1] = (uint8_t)((value >> 48) & 0xFF);
	buffer[2] = (uint8_t)((value >> 40) & 0xFF);
	buffer[3] = (uint8_t)((value >> 32) & 0xFF);
	buffer[4] = (uint8_t)((value >> 24) & 0xFF);
	buffer[5] = (uint8_t)((value >> 16) & 0xFF);
	buffer[6] = (uint8_t)((value >> 8) & 0xFF);
	buffer[7] = (uint8_t)(value & 0xFF);
}

// unpack functions

/**
 * unpacks a 16-bit unsigned integer from a buffer in little-endian format
 * 
 * @param buffer source buffer, must be at least 2 bytes
 * @return: unpacked 16-bit unsigned integer value
 */
static FORCE_INLINE uint16_t unpack_uint16_le(const uint8_t *buffer) {
	return (uint16_t)(buffer[0]) | ((uint16_t)(buffer[1]) << 8);
}

/**
 * unpacks a 16-bit unsigned integer from a buffer in big-endian format
 * 
 * @param buffer source buffer, must be at least 2 bytes
 * @return: unpacked 16-bit unsigned integer value
 */
static FORCE_INLINE uint16_t unpack_uint16_be(const uint8_t *buffer) {
	return ((uint16_t)(buffer[0]) << 8) | (uint16_t)(buffer[1]);
}

/**
 * unpacks a 32-bit unsigned integer from a buffer in little-endian format
 * 
 * @param buffer source buffer, must be at least 4 bytes
 * @return: unpacked 32-bit unsigned integer value
 */
static FORCE_INLINE uint32_t unpack_uint32_le(const uint8_t *buffer) {
	return (uint32_t)(buffer[0]) | ((uint32_t)(buffer[1]) << 8) | ((uint32_t)(buffer[2]) << 16) | ((uint32_t)(buffer[3]) << 24);
}

/**
 * unpacks a 32-bit unsigned integer from a buffer in big-endian format
 * 
 * @param buffer source buffer, must be at least 4 bytes
 * @return: unpacked 32-bit unsigned integer value
 */
static FORCE_INLINE uint32_t unpack_uint32_be(const uint8_t *buffer) {
	return ((uint32_t)(buffer[0]) << 24) | ((uint32_t)(buffer[1]) << 16) | ((uint32_t)(buffer[2]) << 8) | (uint32_t)(buffer[3]);
}

/**
 * unpacks a 64-bit unsigned integer from a buffer in little-endian format
 * 
 * @param buffer source buffer, must be at least 8 bytes
 * @return: unpacked 64-bit unsigned integer value
 */
static FORCE_INLINE uint64_t unpack_uint64_le(const uint8_t *buffer) {
	return (uint64_t)(buffer[0]) | ((uint64_t)(buffer[1]) << 8) | ((uint64_t)(buffer[2]) << 16) | ((uint64_t)(buffer[3]) << 24) | 
	((uint64_t)(buffer[4]) << 32) | ((uint64_t)(buffer[5]) << 40) | ((uint64_t)(buffer[6]) << 48) | ((uint64_t)(buffer[7]) << 56);
}

/**
 * unpacks a 64-bit unsigned integer from a buffer in big-endian format
 * 
 * @param buffer source buffer, must be at least 8 bytes
 * @return: unpacked 64-bit unsigned integer value
 */
static FORCE_INLINE uint64_t unpack_uint64_be(const uint8_t *buffer) {
	return ((uint64_t)(buffer[0]) << 56) | ((uint64_t)(buffer[1]) << 48) | ((uint64_t)(buffer[2]) << 40) | ((uint64_t)(buffer[3]) << 32) | 
	((uint64_t)(buffer[4]) << 24) | ((uint64_t)(buffer[5]) << 16) | ((uint64_t)(buffer[6]) << 8) | (uint64_t)(buffer[7]);
}

// signed integer packing

/**
 * packs a 16-bit signed integer into a buffer in little-endian format
 * 
 * @param buffer destination buffer, must be at least 2 bytes
 * @param value 16-bit signed integer value to pack
 */
static FORCE_INLINE void pack_int16_le(uint8_t *buffer, int16_t value) {
	pack_uint16_le(buffer, (uint16_t)value);
}

/**
 * packs a 16-bit signed integer into a buffer in big-endian format
 * 
 * @param buffer destination buffer, must be at least 2 bytes
 * @param value 16-bit signed integer value to pack
 */
static FORCE_INLINE void pack_int16_be(uint8_t *buffer, int16_t value) {
	pack_uint16_be(buffer, (uint16_t)value);
}

/**
 * packs a 32-bit signed integer into a buffer in little-endian format
 * 
 * @param buffer destination buffer, must be at least 4 bytes
 * @param value 32-bit signed integer value to pack
 */
static FORCE_INLINE void pack_int32_le(uint8_t *buffer, int32_t value) {
	pack_uint32_le(buffer, (uint32_t)value);
}

/**
 * packs a 32-bit signed integer into a buffer in big-endian format
 * 
 * @param buffer destination buffer, must be at least 4 bytes
 * @param value 32-bit signed integer value to pack
 */
static FORCE_INLINE void pack_int32_be(uint8_t *buffer, int32_t value) {
	pack_uint32_be(buffer, (uint32_t)value);
}

/**
 * packs a 64-bit signed integer into a buffer in little-endian format
 * 
 * @param buffer destination buffer, must be at least 8 bytes
 * @param value 64-bit signed integer value to pack
 */
static FORCE_INLINE void pack_int64_le(uint8_t *buffer, int64_t value) {
	pack_uint64_le(buffer, (uint64_t)value);
}

/**
 * packs a 64-bit signed integer into a buffer in big-endian format
 * 
 * @param buffer destination buffer, must be at least 8 bytes
 * @param value 64-bit signed integer value to pack
 */
static FORCE_INLINE void pack_int64_be(uint8_t *buffer, int64_t value) {
	pack_uint64_be(buffer, (uint64_t)value);
}

// signed integer unpacking

/**
 * unpacks a 16-bit signed integer from a buffer in little-endian format
 * 
 * @param buffer source buffer, must be at least 2 bytes
 * @return: unpacked 16-bit signed integer value
 */
static FORCE_INLINE int16_t unpack_int16_le(const uint8_t *buffer) {
	return (int16_t)unpack_uint16_le(buffer);
}

/**
 * unpacks a 16-bit signed integer from a buffer in big-endian format
 * 
 * @param buffer source buffer, must be at least 2 bytes
 * @return: unpacked 16-bit signed integer value
 */
static FORCE_INLINE int16_t unpack_int16_be(const uint8_t *buffer) {
	return (int16_t)unpack_uint16_be(buffer);
}

/**
 * unpacks a 32-bit signed integer from a buffer in little-endian format
 * 
 * @param buffer source buffer, must be at least 4 bytes
 * @return: unpacked 32-bit signed integer value
 */
static FORCE_INLINE int32_t unpack_int32_le(const uint8_t *buffer) {
	return (int32_t)unpack_uint32_le(buffer);
}

/**
 * unpacks a 32-bit signed integer from a buffer in big-endian format
 * 
 * @param buffer source buffer, must be at least 4 bytes
 * @return: unpacked 32-bit signed integer value
 */
static FORCE_INLINE int32_t unpack_int32_be(const uint8_t *buffer) {
	return (int32_t)unpack_uint32_be(buffer);
}

/**
 * unpacks a 64-bit signed integer from a buffer in little-endian format
 * 
 * @param buffer source buffer, must be at least 8 bytes
 * @return: unpacked 64-bit signed integer value
 */
static FORCE_INLINE int64_t unpack_int64_le(const uint8_t *buffer) {
	return (int64_t)unpack_uint64_le(buffer);
}

/**
 * unpacks a 64-bit signed integer from a buffer in big-endian format
 * 
 * @param buffer source buffer, must be at least 8 bytes
 * @return: unpacked 64-bit signed integer value
 */
static FORCE_INLINE int64_t unpack_int64_be(const uint8_t *buffer) {
	return (int64_t)unpack_uint64_be(buffer);
}

#endif /* serialization_h */
