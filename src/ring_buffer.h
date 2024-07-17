#ifndef ring_buffer_h
#define ring_buffer_h

#include <stdint.h>

// thread safe ring buffer
typedef struct ring_buffer_t ring_buffer_t;

// creates a new ring buffer of the specified size
ring_buffer_t *ring_buffer_new(uint32_t size);

// frees the specified ring buffer
void ring_buffer_free(ring_buffer_t **ring_buffer);

// writes data to the ring buffer
uint32_t ring_buffer_write(ring_buffer_t *ring_buffer, const uint8_t *data, uint32_t size);

// reads data from the ring buffer
uint32_t ring_buffer_read(ring_buffer_t *ring_buffer, uint8_t *out_buffer, uint32_t size);

// returns the number of bytes available for reading
uint32_t ring_buffer_read_avail(ring_buffer_t *ring_buffer);

// returns the free space available for writing
uint32_t ring_buffer_write_avail(ring_buffer_t *ring_buffer);

#endif /* ring_buffer_h */
