#include <CUnit/Basic.h>
#include <SDL2/SDL.h>
#include "ring_buffer.h"

#define BUFFER_SIZE 1024
#define NUM_WRITES 10000
#define NUM_READS 10000

typedef struct {
	ring_buffer_t *ring_buffer;
	const uint8_t *data;
	uint32_t data_size;
	uint32_t num_writes;
} writer_args_t;

typedef struct {
	ring_buffer_t *ring_buffer;
	uint8_t *out_buffer;
	uint32_t buffer_size;
	uint32_t num_reads;
} reader_args_t;

int writer_thread(void *args) {
	writer_args_t *wargs = (writer_args_t *)args;
	for (uint32_t i = 0; i < wargs->num_writes; ++i) {
		ring_buffer_write(wargs->ring_buffer, wargs->data, wargs->data_size);
	}
	return 0;
}

int reader_thread(void *args) {
	reader_args_t *rargs = (reader_args_t *)args;
	for (uint32_t i = 0; i < rargs->num_reads; ++i) {
		ring_buffer_read(rargs->ring_buffer, rargs->out_buffer, rargs->buffer_size);
	}
	return 0;
}

void test_ring_buffer_new_free(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	CU_ASSERT_PTR_NOT_NULL(ring_buffer);
	
	ring_buffer_free(&ring_buffer);
	CU_ASSERT_PTR_NULL(ring_buffer);
}

void test_ring_buffer_write_read(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	uint8_t write_data[256] = {0};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 256);
	CU_ASSERT_EQUAL(write_size, 256);
	
	uint8_t read_data[256] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 256);
	CU_ASSERT_EQUAL(read_size, 256);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	CU_ASSERT_EQUAL(overflows, 0);
	CU_ASSERT_EQUAL(underflows, 0);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_write_overflow(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	uint8_t write_data[2048] = {0};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 2048);
	CU_ASSERT_EQUAL(write_size, size);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	CU_ASSERT_EQUAL(overflows, 1024);
	CU_ASSERT_EQUAL(underflows, 0);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_read_empty(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	uint8_t read_data[256] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 256);
	CU_ASSERT_EQUAL(read_size, 0);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	CU_ASSERT_EQUAL(overflows, 0);
	CU_ASSERT_EQUAL(underflows, 256);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_read_avail(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	uint8_t write_data[256] = {0};
	ring_buffer_write(ring_buffer, write_data, 256);
	
	uint32_t read_avail = ring_buffer_read_avail(ring_buffer);
	CU_ASSERT_EQUAL(read_avail, 256);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	CU_ASSERT_EQUAL(overflows, 0);
	CU_ASSERT_EQUAL(underflows, 0);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_write_avail(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	uint8_t write_data[256] = {0};
	ring_buffer_write(ring_buffer, write_data, 256);
	
	uint32_t write_avail = ring_buffer_write_avail(ring_buffer);
	CU_ASSERT_EQUAL(write_avail, 768);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	CU_ASSERT_EQUAL(overflows, 0);
	CU_ASSERT_EQUAL(underflows, 0);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_wraparound(void) {
	uint32_t size = 8; // Small size to test wrapping easily
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	// Write 6 bytes to fill buffer partially
	uint8_t write_data[6] = {1, 2, 3, 4, 5, 6};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 6);
	CU_ASSERT_EQUAL(write_size, 6);
	
	// Read 4 bytes to move tail
	uint8_t read_data[4] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 4);
	CU_ASSERT_EQUAL(read_size, 4);
	
	// Write 4 more bytes to wrap around
	uint8_t write_data2[4] = {7, 8, 9, 10};
	write_size = ring_buffer_write(ring_buffer, write_data2, 4);
	CU_ASSERT_EQUAL(write_size, 4);
	
	// Read all available bytes (6 bytes total, wrapped around)
	uint8_t read_data2[6] = {0};
	read_size = ring_buffer_read(ring_buffer, read_data2, 6);
	CU_ASSERT_EQUAL(read_size, 6);
	
	// Expected read data should be {5, 6, 7, 8, 9, 10}
	CU_ASSERT_EQUAL(read_data2[0], 5);
	CU_ASSERT_EQUAL(read_data2[1], 6);
	CU_ASSERT_EQUAL(read_data2[2], 7);
	CU_ASSERT_EQUAL(read_data2[3], 8);
	CU_ASSERT_EQUAL(read_data2[4], 9);
	CU_ASSERT_EQUAL(read_data2[5], 10);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	CU_ASSERT_EQUAL(overflows, 0);
	CU_ASSERT_EQUAL(underflows, 0);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_concurrent_access(void) {
	ring_buffer_t *ring_buffer = ring_buffer_new(BUFFER_SIZE);
	CU_ASSERT_PTR_NOT_NULL(ring_buffer);
	
	uint8_t write_data[128] = {0};
	uint8_t read_data[128] = {0};
	
	writer_args_t wargs = {
		.ring_buffer = ring_buffer,
		.data = write_data,
		.data_size = sizeof(write_data),
		.num_writes = NUM_WRITES
	};
	
	reader_args_t rargs = {
		.ring_buffer = ring_buffer,
		.out_buffer = read_data,
		.buffer_size = sizeof(read_data),
		.num_reads = NUM_READS
	};
	
	SDL_Thread *writer = SDL_CreateThread(writer_thread, "WriterThread", &wargs);
	SDL_Thread *reader = SDL_CreateThread(reader_thread, "ReaderThread", &rargs);
	
	SDL_WaitThread(writer, NULL);
	SDL_WaitThread(reader, NULL);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_full_write(void) {
	uint32_t size = 8; // Small size to test easily
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	// Fill the buffer completely
	uint8_t write_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 8);
	CU_ASSERT_EQUAL(write_size, 8);
	
	// Attempt to write to a full buffer
	uint8_t extra_data[3] = {9, 10, 11};
	write_size = ring_buffer_write(ring_buffer, extra_data, 3);
	CU_ASSERT_EQUAL(write_size, 0); // Expect to drop all bytes
	
	// Read data back to ensure buffer integrity
	uint8_t read_data[8] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 8);
	CU_ASSERT_EQUAL(read_size, 8);
	
	// Expected read data should be {1, 2, 3, 4, 5, 6, 7, 8}
	for (uint32_t i = 0; i < 8; ++i) {
		CU_ASSERT_EQUAL(read_data[i], i + 1);
	}

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	CU_ASSERT_EQUAL(overflows, 3);
	CU_ASSERT_EQUAL(underflows, 0);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_large_write_read(void) {
	uint32_t size = 8; // Small size to test easily
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	// Write more data than the buffer can hold
	uint8_t write_data[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 12);
	CU_ASSERT_EQUAL(write_size, 8); // Should only write up to buffer capacity
	
	// Read the data back
	uint8_t read_data[8] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 8);
	CU_ASSERT_EQUAL(read_size, 8);
	
	// Expected read data should be the first 8 bytes of write_data
	for (uint32_t i = 0; i < 8; ++i) {
		CU_ASSERT_EQUAL(read_data[i], i + 1);
	}

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	CU_ASSERT_EQUAL(overflows, 4);
	CU_ASSERT_EQUAL(underflows, 0);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_alternating_write_read(void) {
	uint32_t size = 8; // Small size to test easily
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	uint8_t write_data1[4] = {1, 2, 3, 4};
	uint8_t write_data2[4] = {5, 6, 7, 8};
	uint8_t read_data[4] = {0};
	
	// Write first part
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data1, 4);
	CU_ASSERT_EQUAL(write_size, 4);
	
	// Read first part
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 4);
	CU_ASSERT_EQUAL(read_size, 4);
	for (uint32_t i = 0; i < 4; ++i) {
		CU_ASSERT_EQUAL(read_data[i], write_data1[i]);
	}
	
	// Write second part
	write_size = ring_buffer_write(ring_buffer, write_data2, 4);
	CU_ASSERT_EQUAL(write_size, 4);
	
	// Read second part
	read_size = ring_buffer_read(ring_buffer, read_data, 4);
	CU_ASSERT_EQUAL(read_size, 4);
	for (uint32_t i = 0; i < 4; ++i) {
		CU_ASSERT_EQUAL(read_data[i], write_data2[i]);
	}

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	CU_ASSERT_EQUAL(overflows, 0);
	CU_ASSERT_EQUAL(underflows, 0);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_zero_length(void) {
	uint32_t size = 8;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	// Attempt to write zero-length data
	uint8_t write_data[1] = {1};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 0);
	CU_ASSERT_EQUAL(write_size, 0);
	
	// Attempt to read zero-length data
	uint8_t read_data[1] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 0);
	CU_ASSERT_EQUAL(read_size, 0);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	CU_ASSERT_EQUAL(overflows, 0);
	CU_ASSERT_EQUAL(underflows, 0);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_single_byte(void) {
	uint32_t size = 8;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	// Write a single byte
	uint8_t write_data[1] = {1};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 1);
	CU_ASSERT_EQUAL(write_size, 1);
	
	// Read the single byte
	uint8_t read_data[1] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 1);
	CU_ASSERT_EQUAL(read_size, 1);
	CU_ASSERT_EQUAL(read_data[0], 1);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	CU_ASSERT_EQUAL(overflows, 0);
	CU_ASSERT_EQUAL(underflows, 0);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_max_capacity(void) {
	uint32_t size = 8;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	// Write maximum capacity data
	uint8_t write_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 8);
	CU_ASSERT_EQUAL(write_size, 8);
	
	// Read maximum capacity data
	uint8_t read_data[8] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 8);
	CU_ASSERT_EQUAL(read_size, 8);
	
	// Expected read data should match written data
	for (uint32_t i = 0; i < 8; ++i) {
		CU_ASSERT_EQUAL(read_data[i], write_data[i]);
	}

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	CU_ASSERT_EQUAL(overflows, 0);
	CU_ASSERT_EQUAL(underflows, 0);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_boundary_conditions(void) {
	uint32_t size = 8;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	// Write to near capacity
	uint8_t write_data[7] = {1, 2, 3, 4, 5, 6, 7};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 7);
	CU_ASSERT_EQUAL(write_size, 7);
	
	// Read some data to move tail
	uint8_t read_data[3] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 3);
	CU_ASSERT_EQUAL(read_size, 3);
	
	// Write data to wrap around
	uint8_t write_data2[4] = {8, 9, 10, 11};
	write_size = ring_buffer_write(ring_buffer, write_data2, 4);
	CU_ASSERT_EQUAL(write_size, 4);
	
	// Read all data to ensure buffer integrity
	uint8_t read_data2[8] = {0};
	read_size = ring_buffer_read(ring_buffer, read_data2, 8);
	CU_ASSERT_EQUAL(read_size, 8);
	
	// Expected read data should be {4, 5, 6, 7, 8, 9, 10, 11}
	uint8_t expected_data[8] = {4, 5, 6, 7, 8, 9, 10, 11};
	for (uint32_t i = 0; i < 8; ++i) {
		CU_ASSERT_EQUAL(read_data2[i], expected_data[i]);
	}

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	CU_ASSERT_EQUAL(overflows, 0);
	CU_ASSERT_EQUAL(underflows, 0);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_concurrent_writes(void) {
	ring_buffer_t *ring_buffer = ring_buffer_new(BUFFER_SIZE);
	CU_ASSERT_PTR_NOT_NULL(ring_buffer);
	
	uint8_t write_data1[128] = {1};
	uint8_t write_data2[128] = {2};
	
	writer_args_t wargs1 = {
		.ring_buffer = ring_buffer,
		.data = write_data1,
		.data_size = sizeof(write_data1),
		.num_writes = NUM_WRITES
	};
	
	writer_args_t wargs2 = {
		.ring_buffer = ring_buffer,
		.data = write_data2,
		.data_size = sizeof(write_data2),
		.num_writes = NUM_WRITES
	};
	
	SDL_Thread *writer1 = SDL_CreateThread(writer_thread, "WriterThread1", &wargs1);
	SDL_Thread *writer2 = SDL_CreateThread(writer_thread, "WriterThread2", &wargs2);
	
	SDL_WaitThread(writer1, NULL);
	SDL_WaitThread(writer2, NULL);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_concurrent_reads(void) {
	ring_buffer_t *ring_buffer = ring_buffer_new(BUFFER_SIZE);
	CU_ASSERT_PTR_NOT_NULL(ring_buffer);
	
	// Pre-fill the buffer with data
	uint8_t write_data[BUFFER_SIZE] = {0};
	for (uint32_t i = 0; i < BUFFER_SIZE; ++i) {
		write_data[i] = i % 256;
	}
	ring_buffer_write(ring_buffer, write_data, BUFFER_SIZE);
	
	uint8_t read_data1[128] = {0};
	uint8_t read_data2[128] = {0};
	
	reader_args_t rargs1 = {
		.ring_buffer = ring_buffer,
		.out_buffer = read_data1,
		.buffer_size = sizeof(read_data1),
		.num_reads = NUM_READS
	};
	
	reader_args_t rargs2 = {
		.ring_buffer = ring_buffer,
		.out_buffer = read_data2,
		.buffer_size = sizeof(read_data2),
		.num_reads = NUM_READS
	};
	
	SDL_Thread *reader1 = SDL_CreateThread(reader_thread, "ReaderThread1", &rargs1);
	SDL_Thread *reader2 = SDL_CreateThread(reader_thread, "ReaderThread2", &rargs2);
	
	SDL_WaitThread(reader1, NULL);
	SDL_WaitThread(reader2, NULL);
	
	ring_buffer_free(&ring_buffer);
}

int main(void) {
	// Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
		fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
		return 1;
	}
	
	CU_initialize_registry();
	
	CU_pSuite suite = CU_add_suite("ring_buffer_test_suite", 0, 0);
	
	CU_add_test(suite, "test_ring_buffer_new_free", test_ring_buffer_new_free);
	CU_add_test(suite, "test_ring_buffer_write_read", test_ring_buffer_write_read);
	CU_add_test(suite, "test_ring_buffer_write_overflow", test_ring_buffer_write_overflow);
	CU_add_test(suite, "test_ring_buffer_read_empty", test_ring_buffer_read_empty);
	CU_add_test(suite, "test_ring_buffer_read_avail", test_ring_buffer_read_avail);
	CU_add_test(suite, "test_ring_buffer_write_avail", test_ring_buffer_write_avail);
	CU_add_test(suite, "test_ring_buffer_wraparound", test_ring_buffer_wraparound);
	CU_add_test(suite, "test_ring_buffer_concurrent_access", test_ring_buffer_concurrent_access);
	CU_add_test(suite, "test_ring_buffer_full_write", test_ring_buffer_full_write);
	CU_add_test(suite, "test_ring_buffer_large_write_read", test_ring_buffer_large_write_read);
	CU_add_test(suite, "test_ring_buffer_alternating_write_read", test_ring_buffer_alternating_write_read);
	CU_add_test(suite, "test_ring_buffer_zero_length", test_ring_buffer_zero_length);
	CU_add_test(suite, "test_ring_buffer_single_byte", test_ring_buffer_single_byte);
	CU_add_test(suite, "test_ring_buffer_max_capacity", test_ring_buffer_max_capacity);
	CU_add_test(suite, "test_ring_buffer_boundary_conditions", test_ring_buffer_boundary_conditions);
	CU_add_test(suite, "test_ring_buffer_concurrent_writes", test_ring_buffer_concurrent_writes);
	CU_add_test(suite, "test_ring_buffer_concurrent_reads", test_ring_buffer_concurrent_reads);
	
	CU_basic_run_tests();
	CU_cleanup_registry();
	
	// Clean up SDL
	SDL_Quit();
	
	return 0;
}
