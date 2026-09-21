#include "ring_buffer.h"
#include "s9.h"

#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_mutex.h>

// defining the structure for ring buffer
struct ring_buffer_t {
	uint32_t head;
	uint32_t tail;
	uint32_t size;
	uint64_t overflow_bytes;
	uint64_t underflow_bytes;
	SDL_Mutex *mutex;	
	uint8_t buffer[];
};

// macro to get the available data to read in the buffer
#define READ_AVAIL(rb) \
(((rb)->tail > (rb)->head) ? \
((rb)->head + (rb)->size - (rb)->tail) : \
((rb)->head - (rb)->tail))

// macro to get the available space to write in the buffer
#define WRITE_AVAIL(rb) \
(((rb)->tail > (rb)->head) ? \
((rb)->tail - (rb)->head - 1) : \
((rb)->tail + ((rb)->size - 1) - (rb)->head))

// create a new ring buffer with a specified size
ring_buffer_t *ring_buffer_new(uint32_t size) {
	
	// actual ring buffer capacity is 1 byte less than allocated size,
	// in order to differentiate between full and empty states
	size += 1; // compensate
	
	ring_buffer_t *ring_buffer = (ring_buffer_t *)malloc(sizeof(ring_buffer_t) + size);
	CHECK_ALLOC(ring_buffer, return NULL);
	memset(ring_buffer, 0, sizeof(ring_buffer_t) + size);
	
	ring_buffer->head = 0;
	ring_buffer->tail = 0;
	ring_buffer->size = size;
	ring_buffer->mutex = SDL_CreateMutex();
	CHECK_ERROR(ring_buffer->mutex != NULL,
				free(ring_buffer);
				return NULL,
				"failed to create mutex");
	
	return ring_buffer;
}

// free the ring buffer
void ring_buffer_free(ring_buffer_t **ring_buffer) {
	if (ring_buffer && *ring_buffer) {
		if((*ring_buffer)->mutex) {
			SDL_DestroyMutex((*ring_buffer)->mutex);
		}
		free(*ring_buffer);
		*ring_buffer = NULL;
	}
}

// write data to the ring buffer
uint32_t ring_buffer_write(ring_buffer_t *rb, const uint8_t *data, uint32_t size) {
	uint32_t avail;
	
	SDL_LockMutex(rb->mutex);   
	
	avail = WRITE_AVAIL(rb);
	if(size > avail) {
		rb->overflow_bytes += size - avail;
		size = avail;
	}
	
	uint32_t first_part_size = rb->size - rb->head;
	if (size <= first_part_size) {
		memcpy(&rb->buffer[rb->head], data, size);
	} else {
		memcpy(&rb->buffer[rb->head], data, first_part_size);
		memcpy(&rb->buffer[0], data + first_part_size, size - first_part_size);
	}
	rb->head = (rb->head + size) % rb->size;
	
	SDL_UnlockMutex(rb->mutex);
	
	return size;
}

// read data from the ring buffer
uint32_t ring_buffer_read(ring_buffer_t *rb, uint8_t *out_buffer, uint32_t size) {
	uint32_t avail;
	
	SDL_LockMutex(rb->mutex);    
	
	avail = READ_AVAIL(rb);
	if(size > avail) {
		rb->underflow_bytes += size - avail;
		size = avail;
	}
	
	uint32_t first_part_size = rb->size - rb->tail;
	if (size <= first_part_size) {
		memcpy(out_buffer, &rb->buffer[rb->tail], size);
	} else {
		memcpy(out_buffer, &rb->buffer[rb->tail], first_part_size);
		memcpy(out_buffer + first_part_size, &rb->buffer[0], size - first_part_size);
	}
	rb->tail = (rb->tail + size) % rb->size;
	
	SDL_UnlockMutex(rb->mutex);
	
	return size;
}

// get the available data to read, with mutex protection
uint32_t ring_buffer_read_avail(ring_buffer_t *rb) {
	uint32_t avail;
	
	SDL_LockMutex(rb->mutex);
	avail = READ_AVAIL(rb);
	SDL_UnlockMutex(rb->mutex);
	
	return avail;
}

// get the available space to write, with mutex protection
uint32_t ring_buffer_write_avail(ring_buffer_t *rb) {
	uint32_t avail;
	
	SDL_LockMutex(rb->mutex);    
	avail = WRITE_AVAIL(rb);
	SDL_UnlockMutex(rb->mutex);
	
	return avail;
}

// returns number of overflows and underflows
void ring_buffer_get_faults(ring_buffer_t *rb, uint64_t *out_overflow_bytes, uint64_t *out_underflow_bytes) {
	SDL_LockMutex(rb->mutex);
	*out_overflow_bytes = rb->overflow_bytes;
	*out_underflow_bytes = rb->underflow_bytes;
	SDL_UnlockMutex(rb->mutex);
}

// clear number of overflows and underflows
void ring_buffer_clear_faults(ring_buffer_t *rb) {
	SDL_LockMutex(rb->mutex);
	rb->overflow_bytes = rb->underflow_bytes = 0;
	SDL_UnlockMutex(rb->mutex);	
}

