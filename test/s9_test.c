#include <CUnit/Basic.h>
#include <SDL2/SDL.h>
#include "ring_buffer.h"

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
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_write_overflow(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	uint8_t write_data[2048] = {0};
	uint32_t write_size = ring_buffer_write(ring_buffer, write_data, 2048);
	CU_ASSERT_EQUAL(write_size, size);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_read_empty(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	uint8_t read_data[256] = {0};
	uint32_t read_size = ring_buffer_read(ring_buffer, read_data, 256);
	CU_ASSERT_EQUAL(read_size, 0);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_read_avail(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	uint8_t write_data[256] = {0};
	ring_buffer_write(ring_buffer, write_data, 256);
	
	uint32_t read_avail = ring_buffer_read_avail(ring_buffer);
	CU_ASSERT_EQUAL(read_avail, 256);
	
	ring_buffer_free(&ring_buffer);
}

void test_ring_buffer_write_avail(void) {
	uint32_t size = 1024;
	ring_buffer_t *ring_buffer = ring_buffer_new(size);
	
	uint8_t write_data[256] = {0};
	ring_buffer_write(ring_buffer, write_data, 256);
	
	uint32_t write_avail = ring_buffer_write_avail(ring_buffer);
	CU_ASSERT_EQUAL(write_avail, 768);
	
	ring_buffer_free(&ring_buffer);
}

// Test for wrapping when rb->head < rb->tail
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
	
	CU_basic_run_tests();
	CU_cleanup_registry();
	
	// Clean up SDL
	SDL_Quit();
	
	return 0;
}
