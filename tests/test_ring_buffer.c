#include <CUnit/Basic.h>
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

int main() {
    CU_initialize_registry();

    CU_pSuite suite = CU_add_suite("ring_buffer_test_suite", 0, 0);

    CU_add_test(suite, "test_ring_buffer_new_free", test_ring_buffer_new_free);
    CU_add_test(suite, "test_ring_buffer_write_read", test_ring_buffer_write_read);
    CU_add_test(suite, "test_ring_buffer_write_overflow", test_ring_buffer_write_overflow);
    CU_add_test(suite, "test_ring_buffer_read_empty", test_ring_buffer_read_empty);
    CU_add_test(suite, "test_ring_buffer_read_avail", test_ring_buffer_read_avail);
    CU_add_test(suite, "test_ring_buffer_write_avail", test_ring_buffer_write_avail);

    CU_basic_run_tests();
    CU_cleanup_registry();

    return 0;
}
