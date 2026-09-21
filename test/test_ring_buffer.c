#include <assert.h>
#include <stdio.h>
#include <SDL3/SDL.h>
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

static int writer_thread(void *args) {
	writer_args_t *wargs = (writer_args_t *)args;
	for (uint32_t i = 0; i < wargs->num_writes; ++i) {
		ring_buffer_write(wargs->ring_buffer, wargs->data, wargs->data_size);
	}
	return 0;
}

static int reader_thread(void *args) {
	reader_args_t *rargs = (reader_args_t *)args;
	for (uint32_t i = 0; i < rargs->num_reads; ++i) {
		ring_buffer_read(rargs->ring_buffer, rargs->out_buffer, rargs->buffer_size);
	}
	return 0;
}

static void test_ring_buffer_new_free(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	assert(ring_buffer != NULL);

	ring_buffer_free(&ring_buffer);
	assert(ring_buffer == NULL);
}

static void test_ring_buffer_write_read(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);

	uint8_t write_data[256] = {0};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 256);
	assert(write_size == 256);

	uint8_t read_data[256] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 256);
	assert(read_size == 256);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	assert(overflows == 0);
	assert(underflows == 0);

	ring_buffer_free(&ring_buffer);
}

static void test_ring_buffer_write_overflow(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);

	uint8_t write_data[2048] = {0};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 2048);
	assert(write_size == size);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	assert(overflows == 1024);
	assert(underflows == 0);

	ring_buffer_free(&ring_buffer);
}

static void test_ring_buffer_read_empty(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);

	uint8_t read_data[256] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 256);
	assert(read_size == 0);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	assert(overflows == 0);
	assert(underflows == 256);

	ring_buffer_free(&ring_buffer);
}

static void test_ring_buffer_read_avail(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);

	uint8_t write_data[256] = {0};
	ring_buffer_write(ring_buffer, write_data, 256);

	uint32_t read_avail = ring_buffer_read_avail(ring_buffer);
	assert(read_avail == 256);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	assert(overflows == 0);
	assert(underflows == 0);

	ring_buffer_free(&ring_buffer);
}

static void test_ring_buffer_write_avail(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);

	uint8_t write_data[256] = {0};
	ring_buffer_write(ring_buffer, write_data, 256);

	uint32_t write_avail = ring_buffer_write_avail(ring_buffer);
	assert(write_avail == 768);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	assert(overflows == 0);
	assert(underflows == 0);

	ring_buffer_free(&ring_buffer);
}

static void test_ring_buffer_wraparound(void) {
	uint32_t size = 8;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);

	uint8_t write_data[6] = {1, 2, 3, 4, 5, 6};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 6);
	assert(write_size == 6);

	uint8_t read_data[4] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 4);
	assert(read_size == 4);

	uint8_t write_data2[4] = {7, 8, 9, 10};
	write_size = ring_buffer_write(ring_buffer, write_data2, 4);
	assert(write_size == 4);

	uint8_t read_data2[6] = {0};
	read_size = ring_buffer_read(ring_buffer, read_data2, 6);
	assert(read_size == 6);

	assert(read_data2[0] == 5);
	assert(read_data2[1] == 6);
	assert(read_data2[2] == 7);
	assert(read_data2[3] == 8);
	assert(read_data2[4] == 9);
	assert(read_data2[5] == 10);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	assert(overflows == 0);
	assert(underflows == 0);

	ring_buffer_free(&ring_buffer);
}

static void test_ring_buffer_concurrent_access(void) {
	ring_buffer_t *ring_buffer = ring_buffer_new(BUFFER_SIZE);
	assert(ring_buffer != NULL);

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

static void test_ring_buffer_full_write(void) {
	uint32_t size = 8;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);

	uint8_t write_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 8);
	assert(write_size == 8);

	uint8_t extra_data[3] = {9, 10, 11};
	write_size = ring_buffer_write(ring_buffer, extra_data, 3);
	assert(write_size == 0);

	uint8_t read_data[8] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 8);
	assert(read_size == 8);

	for (uint32_t i = 0; i < 8; ++i) {
		assert(read_data[i] == i + 1);
	}

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	assert(overflows == 3);
	assert(underflows == 0);

	ring_buffer_free(&ring_buffer);
}

static void test_ring_buffer_large_write_read(void) {
	uint32_t size = 8;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);

	uint8_t write_data[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 12);
	assert(write_size == 8);

	uint8_t read_data[8] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 8);
	assert(read_size == 8);

	for (uint32_t i = 0; i < 8; ++i) {
		assert(read_data[i] == i + 1);
	}

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	assert(overflows == 4);
	assert(underflows == 0);

	ring_buffer_free(&ring_buffer);
}

static void test_ring_buffer_alternating_write_read(void) {
	uint32_t size = 8;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);

	uint8_t write_data1[4] = {1, 2, 3, 4};
	uint8_t write_data2[4] = {5, 6, 7, 8};
	uint8_t read_data[4] = {0};

	uint32_t write_size = ring_buffer_write(ring_buffer, write_data1, 4);
	assert(write_size == 4);

	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 4);
	assert(read_size == 4);
	for (uint32_t i = 0; i < 4; ++i) {
		assert(read_data[i] == write_data1[i]);
	}

	write_size = ring_buffer_write(ring_buffer, write_data2, 4);
	assert(write_size == 4);

	read_size = ring_buffer_read(ring_buffer, read_data, 4);
	assert(read_size == 4);
	for (uint32_t i = 0; i < 4; ++i) {
		assert(read_data[i] == write_data2[i]);
	}

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	assert(overflows == 0);
	assert(underflows == 0);

	ring_buffer_free(&ring_buffer);
}

static void test_ring_buffer_zero_length(void) {
	uint32_t size = 8;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);

	uint8_t write_data[1] = {1};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 0);
	assert(write_size == 0);

	uint8_t read_data[1] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 0);
	assert(read_size == 0);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	assert(overflows == 0);
	assert(underflows == 0);

	ring_buffer_free(&ring_buffer);
}

static void test_ring_buffer_single_byte(void) {
	uint32_t size = 8;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);

	uint8_t write_data[1] = {1};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 1);
	assert(write_size == 1);

	uint8_t read_data[1] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 1);
	assert(read_size == 1);
	assert(read_data[0] == 1);

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	assert(overflows == 0);
	assert(underflows == 0);

	ring_buffer_free(&ring_buffer);
}

static void test_ring_buffer_max_capacity(void) {
	uint32_t size = 8;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);

	uint8_t write_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 8);
	assert(write_size == 8);

	uint8_t read_data[8] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 8);
	assert(read_size == 8);

	for (uint32_t i = 0; i < 8; ++i) {
		assert(read_data[i] == write_data[i]);
	}

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	assert(overflows == 0);
	assert(underflows == 0);

	ring_buffer_free(&ring_buffer);
}

static void test_ring_buffer_boundary_conditions(void) {
	uint32_t size = 8;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);

	uint8_t write_data[7] = {1, 2, 3, 4, 5, 6, 7};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 7);
	assert(write_size == 7);

	uint8_t read_data[3] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 3);
	assert(read_size == 3);

	uint8_t write_data2[4] = {8, 9, 10, 11};
	write_size = ring_buffer_write(ring_buffer, write_data2, 4);
	assert(write_size == 4);

	uint8_t read_data2[8] = {0};
	read_size = ring_buffer_read(ring_buffer, read_data2, 8);
	assert(read_size == 8);

	uint8_t expected_data[8] = {4, 5, 6, 7, 8, 9, 10, 11};
	for (uint32_t i = 0; i < 8; ++i) {
		assert(read_data2[i] == expected_data[i]);
	}

	uint64_t overflows = 0, underflows = 0;
	ring_buffer_get_faults(ring_buffer, &overflows, &underflows);
	assert(overflows == 0);
	assert(underflows == 0);

	ring_buffer_free(&ring_buffer);
}

static void test_ring_buffer_concurrent_writes(void) {
	ring_buffer_t *ring_buffer = ring_buffer_new(BUFFER_SIZE);
	assert(ring_buffer != NULL);

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

static void test_ring_buffer_concurrent_reads(void) {
	ring_buffer_t *ring_buffer = ring_buffer_new(BUFFER_SIZE);
	assert(ring_buffer != NULL);

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

#define RUN_TEST(test) do { \
	printf("  %s...", #test); \
	test(); \
	printf(" ok\n"); \
} while(0)

int main(void) {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
		fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
		return 1;
	}

	printf("ring_buffer tests:\n");

	RUN_TEST(test_ring_buffer_new_free);
	RUN_TEST(test_ring_buffer_write_read);
	RUN_TEST(test_ring_buffer_write_overflow);
	RUN_TEST(test_ring_buffer_read_empty);
	RUN_TEST(test_ring_buffer_read_avail);
	RUN_TEST(test_ring_buffer_write_avail);
	RUN_TEST(test_ring_buffer_wraparound);
	RUN_TEST(test_ring_buffer_concurrent_access);
	RUN_TEST(test_ring_buffer_full_write);
	RUN_TEST(test_ring_buffer_large_write_read);
	RUN_TEST(test_ring_buffer_alternating_write_read);
	RUN_TEST(test_ring_buffer_zero_length);
	RUN_TEST(test_ring_buffer_single_byte);
	RUN_TEST(test_ring_buffer_max_capacity);
	RUN_TEST(test_ring_buffer_boundary_conditions);
	RUN_TEST(test_ring_buffer_concurrent_writes);
	RUN_TEST(test_ring_buffer_concurrent_reads);

	printf("\nAll tests passed.\n");

	SDL_Quit();
	return 0;
}
