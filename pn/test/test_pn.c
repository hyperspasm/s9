#include <pn/pn.h>
#include "pn_crc.h"
#include "pn_cobs.h"
#include "pn_param.h"
#include "pn_fnv.h"
#include "amp_tree.h"
#include <stdio.h>
#include <string.h>
#include <stdalign.h>

// --- Test framework ---

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name) \
	do { \
		tests_run++; \
		int rc = name(); \
		if (rc) { tests_failed++; printf("  FAIL: %s\n", #name); } \
		else { printf("  pass: %s\n", #name); } \
	} while (0)

#define ASSERT(cond) \
	do { if (!(cond)) { \
		printf("    assert failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
		return 1; \
	}} while (0)

#define ASSERT_EQ(a, b) \
	do { if ((a) != (b)) { \
		printf("    assert failed: %s == %s (%ld != %ld) (%s:%d)\n", \
			   #a, #b, (long)(a), (long)(b), __FILE__, __LINE__); \
		return 1; \
	}} while (0)

#define ASSERT_MEM(a, b, n) \
	do { if (memcmp(a, b, n) != 0) { \
		printf("    assert failed: memcmp(%s, %s, %u) (%s:%d)\n", \
			   #a, #b, (unsigned)(n), __FILE__, __LINE__); \
		return 1; \
	}} while (0)

#define ASSERT_STR(a, b) \
	do { if (strcmp((a), (b)) != 0) { \
		printf("    assert failed: strcmp(%s, %s) (\"%s\" != \"%s\") (%s:%d)\n", \
			   #a, #b, (a), (b), __FILE__, __LINE__); \
		return 1; \
	}} while (0)

#define ASSERT_FLOAT(a, b) \
	do { if ((a) != (b)) { \
		printf("    assert failed: %s == %s (%g != %g) (%s:%d)\n", \
			   #a, #b, (double)(a), (double)(b), __FILE__, __LINE__); \
		return 1; \
	}} while (0)

// === CRC tests ===

static int test_crc_empty(void) {
	uint8_t dummy = 0;
	ASSERT_EQ(pn_crc16(&dummy, 0), 0xFFFF);
	return 0;
}

static int test_crc_known_vector(void) {
	// Standard CRC-16/CCITT-FALSE check value for "123456789"
	const uint8_t data[] = "123456789";
	ASSERT_EQ(pn_crc16(data, 9), 0x29B1);
	return 0;
}

static int test_crc_single_byte(void) {
	const uint8_t a = 0x00;
	const uint8_t b = 0xFF;
	// Different inputs should produce different CRCs
	ASSERT(pn_crc16(&a, 1) != pn_crc16(&b, 1));
	return 0;
}

static int test_crc_detects_change(void) {
	uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
	uint16_t crc1 = pn_crc16(data, 4);
	data[2] = 0xFF;
	uint16_t crc2 = pn_crc16(data, 4);
	ASSERT(crc1 != crc2);
	return 0;
}

static int test_crc_detects_reorder(void) {
	const uint8_t a[] = { 0x01, 0x02 };
	const uint8_t b[] = { 0x02, 0x01 };
	ASSERT(pn_crc16(a, 2) != pn_crc16(b, 2));
	return 0;
}

// === COBS test helpers ===

typedef struct {
	uint8_t buf[1024];
	uint32_t len;
	int count;
} test_rx_t;

static void test_on_packet(const uint8_t *data, uint32_t len, void *ctx) {
	test_rx_t *rx = ctx;
	if (len <= sizeof(rx->buf)) {
		memcpy(rx->buf, data, len);
		rx->len = len;
	}
	rx->count++;
}

#define MAX_PKT 512

static pn_cobs_t *make_cobs(uint8_t *mem, test_rx_t *rx) {
	pn_cobs_config_t config = {
		.max_packet_size = MAX_PKT,
		.on_packet = test_on_packet,
		.ctx = rx,
	};
	pn_cobs_t *cobs = (pn_cobs_t *)mem;
	pn_cobs_init(cobs, &config);
	return cobs;
}

// Round-trip helper: encode, feed, verify callback receives original data
static int roundtrip(pn_cobs_t *cobs, test_rx_t *rx,
					 const uint8_t *data, uint32_t len) {
	uint8_t enc[PN_COBS_MAX_ENCODED_SIZE(MAX_PKT)];
	uint32_t enc_len = sizeof(enc);

	rx->count = 0;
	rx->len = 0;

	int rc = pn_cobs_encode(cobs, data, len, enc, &enc_len);
	if (rc != 0) return 1;

	rc = pn_cobs_feed(cobs, enc, enc_len);
	if (rc != 0) return 1;

	if (rx->count != 1) return 1;
	if (rx->len != len) return 1;
	if (len > 0 && memcmp(rx->buf, data, len) != 0) return 1;

	return 0;
}

// === COBS tests ===

static int test_cobs_roundtrip_basic(void) {
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(&(pn_cobs_config_t){ .max_packet_size = MAX_PKT })];
	test_rx_t rx = {0};
	pn_cobs_t *cobs = make_cobs(mem, &rx);

	const uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
	ASSERT_EQ(roundtrip(cobs, &rx, data, sizeof(data)), 0);
	return 0;
}

static int test_cobs_roundtrip_empty(void) {
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(&(pn_cobs_config_t){ .max_packet_size = MAX_PKT })];
	test_rx_t rx = {0};
	pn_cobs_t *cobs = make_cobs(mem, &rx);

	ASSERT_EQ(roundtrip(cobs, &rx, (const uint8_t *)"", 0), 0);
	return 0;
}

static int test_cobs_roundtrip_single_zero(void) {
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(&(pn_cobs_config_t){ .max_packet_size = MAX_PKT })];
	test_rx_t rx = {0};
	pn_cobs_t *cobs = make_cobs(mem, &rx);

	const uint8_t data[] = { 0x00 };
	ASSERT_EQ(roundtrip(cobs, &rx, data, 1), 0);
	return 0;
}

static int test_cobs_roundtrip_all_zeros(void) {
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(&(pn_cobs_config_t){ .max_packet_size = MAX_PKT })];
	test_rx_t rx = {0};
	pn_cobs_t *cobs = make_cobs(mem, &rx);

	uint8_t data[32];
	memset(data, 0, sizeof(data));
	ASSERT_EQ(roundtrip(cobs, &rx, data, sizeof(data)), 0);
	return 0;
}

static int test_cobs_roundtrip_no_zeros(void) {
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(&(pn_cobs_config_t){ .max_packet_size = MAX_PKT })];
	test_rx_t rx = {0};
	pn_cobs_t *cobs = make_cobs(mem, &rx);

	uint8_t data[64];
	for (uint32_t i = 0; i < sizeof(data); i++)
		data[i] = (uint8_t)(i + 1);
	ASSERT_EQ(roundtrip(cobs, &rx, data, sizeof(data)), 0);
	return 0;
}

static int test_cobs_roundtrip_254_bytes(void) {
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(&(pn_cobs_config_t){ .max_packet_size = MAX_PKT })];
	test_rx_t rx = {0};
	pn_cobs_t *cobs = make_cobs(mem, &rx);

	// 254 non-zero bytes — exactly one COBS block
	uint8_t data[254];
	for (uint32_t i = 0; i < 254; i++)
		data[i] = (uint8_t)(i + 1);
	ASSERT_EQ(roundtrip(cobs, &rx, data, 254), 0);
	return 0;
}

static int test_cobs_roundtrip_255_bytes(void) {
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(&(pn_cobs_config_t){ .max_packet_size = MAX_PKT })];
	test_rx_t rx = {0};
	pn_cobs_t *cobs = make_cobs(mem, &rx);

	// 255 non-zero bytes — crosses a COBS block boundary
	uint8_t data[255];
	for (uint32_t i = 0; i < 255; i++)
		data[i] = (uint8_t)(i + 1);
	ASSERT_EQ(roundtrip(cobs, &rx, data, 255), 0);
	return 0;
}

static int test_cobs_roundtrip_zeros_at_boundaries(void) {
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(&(pn_cobs_config_t){ .max_packet_size = MAX_PKT })];
	test_rx_t rx = {0};
	pn_cobs_t *cobs = make_cobs(mem, &rx);

	// Zeros at start, middle, and end
	uint8_t data[] = { 0x00, 0x01, 0x02, 0x00, 0x03, 0x00 };
	ASSERT_EQ(roundtrip(cobs, &rx, data, sizeof(data)), 0);
	return 0;
}

static int test_cobs_multiple_frames(void) {
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(&(pn_cobs_config_t){ .max_packet_size = MAX_PKT })];
	test_rx_t rx = {0};
	pn_cobs_t *cobs = make_cobs(mem, &rx);

	const uint8_t data1[] = { 0xAA, 0xBB };
	const uint8_t data2[] = { 0xCC, 0xDD, 0xEE };

	// Encode both frames into one buffer
	uint8_t enc[PN_COBS_MAX_ENCODED_SIZE(MAX_PKT) * 2];
	uint32_t len1 = sizeof(enc);
	ASSERT_EQ(pn_cobs_encode(cobs, data1, sizeof(data1), enc, &len1), 0);

	uint32_t len2 = (uint32_t)(sizeof(enc) - len1);
	ASSERT_EQ(pn_cobs_encode(cobs, data2, sizeof(data2), enc + len1, &len2), 0);

	// Feed everything at once
	rx.count = 0;
	ASSERT_EQ(pn_cobs_feed(cobs, enc, len1 + len2), 0);
	ASSERT_EQ(rx.count, 2);

	// Last delivered packet should be data2
	ASSERT_EQ(rx.len, sizeof(data2));
	ASSERT_MEM(rx.buf, data2, sizeof(data2));
	return 0;
}

static int test_cobs_consecutive_delimiters(void) {
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(&(pn_cobs_config_t){ .max_packet_size = MAX_PKT })];
	test_rx_t rx = {0};
	pn_cobs_t *cobs = make_cobs(mem, &rx);

	// Leading delimiters should be ignored
	const uint8_t data[] = { 0x01, 0x02 };
	uint8_t enc[PN_COBS_MAX_ENCODED_SIZE(MAX_PKT)];
	uint32_t enc_len = sizeof(enc);
	ASSERT_EQ(pn_cobs_encode(cobs, data, sizeof(data), enc, &enc_len), 0);

	// Prepend extra delimiters
	uint8_t buf[64];
	buf[0] = 0x00;
	buf[1] = 0x00;
	buf[2] = 0x00;
	memcpy(buf + 3, enc, enc_len);

	rx.count = 0;
	ASSERT_EQ(pn_cobs_feed(cobs, buf, 3 + enc_len), 0);
	ASSERT_EQ(rx.count, 1);
	ASSERT_EQ(rx.len, sizeof(data));
	ASSERT_MEM(rx.buf, data, sizeof(data));
	return 0;
}

static int test_cobs_corrupted_crc(void) {
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(&(pn_cobs_config_t){ .max_packet_size = MAX_PKT })];
	test_rx_t rx = {0};
	pn_cobs_t *cobs = make_cobs(mem, &rx);

	const uint8_t data[] = { 0x01, 0x02, 0x03 };
	uint8_t enc[PN_COBS_MAX_ENCODED_SIZE(MAX_PKT)];
	uint32_t enc_len = sizeof(enc);
	ASSERT_EQ(pn_cobs_encode(cobs, data, sizeof(data), enc, &enc_len), 0);

	// Corrupt a byte in the encoded frame (before the delimiter)
	enc[1] ^= 0xFF;

	rx.count = 0;
	pn_cobs_feed(cobs, enc, enc_len);
	ASSERT_EQ(rx.count, 0); // should not deliver
	return 0;
}

static int test_cobs_encode_too_large(void) {
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(&(pn_cobs_config_t){ .max_packet_size = MAX_PKT })];
	test_rx_t rx = {0};
	pn_cobs_t *cobs = make_cobs(mem, &rx);

	uint8_t data[MAX_PKT + 1];
	memset(data, 0xAA, sizeof(data));
	uint8_t enc[PN_COBS_MAX_ENCODED_SIZE(MAX_PKT + 1)];
	uint32_t enc_len = sizeof(enc);

	ASSERT_EQ(pn_cobs_encode(cobs, data, sizeof(data), enc, &enc_len), -1);
	return 0;
}

static int test_cobs_encode_buffer_too_small(void) {
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(&(pn_cobs_config_t){ .max_packet_size = MAX_PKT })];
	test_rx_t rx = {0};
	pn_cobs_t *cobs = make_cobs(mem, &rx);

	const uint8_t data[] = { 0x01, 0x02, 0x03 };
	uint8_t enc[4]; // too small
	uint32_t enc_len = sizeof(enc);

	ASSERT_EQ(pn_cobs_encode(cobs, data, sizeof(data), enc, &enc_len), -1);
	return 0;
}

static int test_cobs_byte_at_a_time(void) {
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(&(pn_cobs_config_t){ .max_packet_size = MAX_PKT })];
	test_rx_t rx = {0};
	pn_cobs_t *cobs = make_cobs(mem, &rx);

	const uint8_t data[] = { 0xDE, 0xAD, 0x00, 0xBE, 0xEF };
	uint8_t enc[PN_COBS_MAX_ENCODED_SIZE(MAX_PKT)];
	uint32_t enc_len = sizeof(enc);
	ASSERT_EQ(pn_cobs_encode(cobs, data, sizeof(data), enc, &enc_len), 0);

	// Feed one byte at a time
	rx.count = 0;
	for (uint32_t i = 0; i < enc_len; i++)
		pn_cobs_feed(cobs, &enc[i], 1);

	ASSERT_EQ(rx.count, 1);
	ASSERT_EQ(rx.len, sizeof(data));
	ASSERT_MEM(rx.buf, data, sizeof(data));
	return 0;
}

static int test_cobs_overflow_discards(void) {
	// Use a small buffer so a max-size packet's COBS encoding fills it exactly.
	// Then append an extra byte before the delimiter to trigger overflow.
	// The truncated frame (without the extra byte) is still valid COBS with
	// correct CRC — so this test fails if overflow isn't detected.
	enum { SMALL_PKT = 4 };
	alignas(max_align_t) uint8_t mem[pn_cobs_alloc_size(
		&(pn_cobs_config_t){ .max_packet_size = SMALL_PKT })];
	test_rx_t rx = {0};
	pn_cobs_config_t cfg = {
		.max_packet_size = SMALL_PKT,
		.on_packet = test_on_packet,
		.ctx = &rx,
	};
	pn_cobs_t *cobs = (pn_cobs_t *)mem;
	pn_cobs_init(cobs, &cfg);

	const uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
	uint8_t enc[PN_COBS_MAX_ENCODED_SIZE(SMALL_PKT)];
	uint32_t enc_len = sizeof(enc);
	ASSERT_EQ(pn_cobs_encode(cobs, data, sizeof(data), enc, &enc_len), 0);

	// Strip delimiter, append extra byte, re-add delimiter
	uint32_t frame_len = enc_len - 1;
	uint8_t overflow[PN_COBS_MAX_ENCODED_SIZE(SMALL_PKT) + 2];
	memcpy(overflow, enc, frame_len);
	overflow[frame_len] = 0xFF;      // overflow byte
	overflow[frame_len + 1] = 0x00;  // delimiter

	rx.count = 0;
	pn_cobs_feed(cobs, overflow, frame_len + 2);
	ASSERT_EQ(rx.count, 0); // must reject — frame overflowed

	// Recovery: next valid frame should decode normally
	enc_len = sizeof(enc);
	ASSERT_EQ(pn_cobs_encode(cobs, data, sizeof(data), enc, &enc_len), 0);
	rx.count = 0;
	rx.len = 0;
	pn_cobs_feed(cobs, enc, enc_len);
	ASSERT_EQ(rx.count, 1);
	ASSERT_EQ(rx.len, sizeof(data));
	ASSERT_MEM(rx.buf, data, sizeof(data));

	return 0;
}

// === Param test helpers ===

// Generic getters/setters — ctx points to the backing variable
static void     set_f_fn(void *ctx, float v)       { *(float *)ctx = v; }
static float    get_f_fn(void *ctx)                 { return *(float *)ctx; }
static void     set_i_fn(void *ctx, int32_t v)      { *(int32_t *)ctx = v; }
static int32_t  get_i_fn(void *ctx)                 { return *(int32_t *)ctx; }
static void     set_b_fn(void *ctx, bool v)         { *(bool *)ctx = v; }
static bool     get_b_fn(void *ctx)                 { return *(bool *)ctx; }
static void     set_e_fn(void *ctx, uint32_t v)     { *(uint32_t *)ctx = v; }
static uint32_t get_e_fn(void *ctx)                 { return *(uint32_t *)ctx; }
static void     set_s_fn(void *ctx, const char *v)  { strncpy(ctx, v, 31); ((char *)ctx)[31] = '\0'; }
static const char *get_s_fn(void *ctx)              { return (const char *)ctx; }

static int action_fired;
static void action_fn(void *ctx) { (void)ctx; action_fired++; }

static const char *test_input_labels[] = { "mic", "line" };

// Test tree:
//   root (group)
//   +-- gain (float, -72..6)
//   +-- mute (bool)
//   +-- input (enum: mic, line)
//   +-- name (string, max 32)
//   +-- reset (action)
//   +-- sub (group)
//       +-- level (int, 0..100)
//       +-- pan (float, -1..1)

typedef struct {
	float gain;
	bool mute;
	uint32_t input;
	char name[32];
	int32_t level;
	float pan;
	pn_param_t sub_nodes[2];
	pn_param_t nodes[6];
	pn_param_t root;
} test_tree_t;

static void test_tree_init(test_tree_t *t) {
	memset(t, 0, sizeof(*t));
	t->level = 50;
	t->sub_nodes[0] = pn_int("level", &t->level, set_i_fn, get_i_fn, 0, 100, 1);
	t->sub_nodes[1] = pn_float("pan", &t->pan, set_f_fn, get_f_fn, -1.0f, 1.0f, 0.01f);
	t->nodes[0] = pn_float("gain", &t->gain, set_f_fn, get_f_fn, -72.0f, 6.0f, 0.1f);
	t->nodes[1] = pn_bool("mute", &t->mute, set_b_fn, get_b_fn);
	t->nodes[2] = pn_enum("input", &t->input, set_e_fn, get_e_fn, test_input_labels, 2);
	t->nodes[3] = pn_string("name", t->name, set_s_fn, get_s_fn, 32);
	t->nodes[4] = pn_action("reset", NULL, action_fn);
	t->nodes[5] = pn_group("sub", t->sub_nodes, 2);
	t->root = pn_group("root", t->nodes, 6);
}

// === Param find tests ===

static int test_find_root(void) {
	test_tree_t t;
	test_tree_init(&t);
	ASSERT(pn_param_find(&t.root, "root") == &t.root);
	return 0;
}

static int test_find_leaf(void) {
	test_tree_t t;
	test_tree_init(&t);
	const pn_param_t *p = pn_param_find(&t.root, "root/gain");
	ASSERT(p != NULL);
	ASSERT_STR(p->node.name, "gain");
	ASSERT_EQ(p->node.type, PN_FLOAT);
	return 0;
}

static int test_find_nested(void) {
	test_tree_t t;
	test_tree_init(&t);
	const pn_param_t *p = pn_param_find(&t.root, "root/sub/level");
	ASSERT(p != NULL);
	ASSERT_STR(p->node.name, "level");
	ASSERT_EQ(p->node.type, PN_INT);
	return 0;
}

static int test_find_wrong_root(void) {
	test_tree_t t;
	test_tree_init(&t);
	ASSERT(pn_param_find(&t.root, "other/gain") == NULL);
	return 0;
}

static int test_find_nonexistent(void) {
	test_tree_t t;
	test_tree_init(&t);
	ASSERT(pn_param_find(&t.root, "root/nope") == NULL);
	return 0;
}

static int test_find_slashes(void) {
	test_tree_t t;
	test_tree_init(&t);
	// Leading slashes
	ASSERT(pn_param_find(&t.root, "/root/gain") != NULL);
	// Trailing slashes
	ASSERT(pn_param_find(&t.root, "root/gain/") != NULL);
	// Consecutive slashes
	ASSERT(pn_param_find(&t.root, "root//gain") != NULL);
	// All combined
	ASSERT(pn_param_find(&t.root, "//root///gain//") != NULL);
	return 0;
}

static int test_find_null(void) {
	test_tree_t t;
	test_tree_init(&t);
	ASSERT(pn_param_find(&t.root, NULL) == NULL);
	ASSERT(pn_param_find(NULL, "root") == NULL);
	ASSERT(pn_param_find(&t.root, "") == NULL);
	return 0;
}

// === Param set/get tests ===

static int test_setget_float(void) {
	test_tree_t t;
	test_tree_init(&t);
	pn_value_t v = { .f = 3.0f };
	ASSERT_EQ(pn_param_set(&t.nodes[0], &v), 0);
	ASSERT_FLOAT(t.gain, 3.0f);
	pn_value_t out = {0};
	ASSERT_EQ(pn_param_get(&t.nodes[0], &out), 0);
	ASSERT_FLOAT(out.f, 3.0f);
	return 0;
}

static int test_setget_int(void) {
	test_tree_t t;
	test_tree_init(&t);
	pn_value_t v = { .i = 75 };
	ASSERT_EQ(pn_param_set(&t.sub_nodes[0], &v), 0);
	ASSERT_EQ(t.level, 75);
	pn_value_t out = {0};
	ASSERT_EQ(pn_param_get(&t.sub_nodes[0], &out), 0);
	ASSERT_EQ(out.i, 75);
	return 0;
}

static int test_setget_bool(void) {
	test_tree_t t;
	test_tree_init(&t);
	pn_value_t v = { .b = true };
	ASSERT_EQ(pn_param_set(&t.nodes[1], &v), 0);
	ASSERT(t.mute == true);
	pn_value_t out = {0};
	ASSERT_EQ(pn_param_get(&t.nodes[1], &out), 0);
	ASSERT(out.b == true);
	return 0;
}

static int test_setget_enum(void) {
	test_tree_t t;
	test_tree_init(&t);
	pn_value_t v = { .e = 1 };
	ASSERT_EQ(pn_param_set(&t.nodes[2], &v), 0);
	ASSERT_EQ(t.input, 1);
	pn_value_t out = {0};
	ASSERT_EQ(pn_param_get(&t.nodes[2], &out), 0);
	ASSERT_EQ(out.e, 1);
	return 0;
}

static int test_setget_string(void) {
	test_tree_t t;
	test_tree_init(&t);
	pn_value_t v = { .s = "hello" };
	ASSERT_EQ(pn_param_set(&t.nodes[3], &v), 0);
	ASSERT_STR(t.name, "hello");
	pn_value_t out = {0};
	ASSERT_EQ(pn_param_get(&t.nodes[3], &out), 0);
	ASSERT_STR(out.s, "hello");
	return 0;
}

static int test_clamp_float(void) {
	test_tree_t t;
	test_tree_init(&t);
	pn_value_t v = { .f = -100.0f };
	pn_param_set(&t.nodes[0], &v);
	ASSERT_FLOAT(t.gain, -72.0f);
	v.f = 100.0f;
	pn_param_set(&t.nodes[0], &v);
	ASSERT_FLOAT(t.gain, 6.0f);
	return 0;
}

static int test_clamp_int(void) {
	test_tree_t t;
	test_tree_init(&t);
	pn_value_t v = { .i = -10 };
	pn_param_set(&t.sub_nodes[0], &v);
	ASSERT_EQ(t.level, 0);
	v.i = 200;
	pn_param_set(&t.sub_nodes[0], &v);
	ASSERT_EQ(t.level, 100);
	return 0;
}

static int test_clamp_enum(void) {
	test_tree_t t;
	test_tree_init(&t);
	pn_value_t v = { .e = 99 };
	pn_param_set(&t.nodes[2], &v);
	ASSERT_EQ(t.input, 1); // clamped to count-1
	return 0;
}

static int test_action(void) {
	test_tree_t t;
	test_tree_init(&t);
	action_fired = 0;
	ASSERT_EQ(pn_param_set(&t.nodes[4], NULL), 0);
	ASSERT_EQ(action_fired, 1);
	return 0;
}

static int test_set_no_setter(void) {
	pn_param_t p = { .node = { .name = "ro", .type = PN_FLOAT } };
	pn_value_t v = { .f = 1.0f };
	ASSERT_EQ(pn_param_set(&p, &v), -1);
	return 0;
}

static int test_get_no_getter(void) {
	pn_param_t p = { .node = { .name = "wo", .type = PN_FLOAT } };
	pn_value_t v = {0};
	ASSERT_EQ(pn_param_get(&p, &v), -1);
	return 0;
}

static int test_enum_zero_count(void) {
	pn_param_t p = pn_enum("empty", NULL, set_e_fn, get_e_fn, NULL, 0);
	pn_value_t v = { .e = 0 };
	ASSERT_EQ(pn_param_set(&p, &v), -1);
	return 0;
}

static int test_set_group_rejects(void) {
	test_tree_t t;
	test_tree_init(&t);
	pn_value_t v = { .i = 1 };
	ASSERT_EQ(pn_param_set(&t.root, &v), -1);
	ASSERT_EQ(pn_param_get(&t.root, &v), -1);
	return 0;
}

// === Walk tests ===

typedef struct {
	const char *names[16];
	int count;
} walk_record_t;

static int record_visitor(const pn_param_t *param,
						  const pn_param_t **path,
						  uint32_t depth, void *ctx) {
	(void)path; (void)depth;
	walk_record_t *r = ctx;
	if (r->count < 16)
		r->names[r->count++] = param->node.name;
	return 0;
}

static int test_walk_visits_all(void) {
	test_tree_t t;
	test_tree_init(&t);

	walk_record_t rec = {0};
	const pn_param_t *path[PN_MAX_DEPTH];
	ASSERT_EQ(pn_param_walk(&t.root, record_visitor, &rec, path, PN_MAX_DEPTH), 0);

	// DFS pre-order: root, gain, mute, input, name, reset, sub, level, pan
	ASSERT_EQ(rec.count, 9);
	ASSERT_STR(rec.names[0], "root");
	ASSERT_STR(rec.names[1], "gain");
	ASSERT_STR(rec.names[2], "mute");
	ASSERT_STR(rec.names[3], "input");
	ASSERT_STR(rec.names[4], "name");
	ASSERT_STR(rec.names[5], "reset");
	ASSERT_STR(rec.names[6], "sub");
	ASSERT_STR(rec.names[7], "level");
	ASSERT_STR(rec.names[8], "pan");
	return 0;
}

static int stop_visitor(const pn_param_t *param,
						const pn_param_t **path,
						uint32_t depth, void *ctx) {
	(void)path; (void)depth;
	walk_record_t *r = ctx;
	if (r->count < 16)
		r->names[r->count++] = param->node.name;
	if (strcmp(param->node.name, "mute") == 0)
		return 1;
	return 0;
}

static int test_walk_early_stop(void) {
	test_tree_t t;
	test_tree_init(&t);

	walk_record_t rec = {0};
	const pn_param_t *path[PN_MAX_DEPTH];
	int rc = pn_param_walk(&t.root, stop_visitor, &rec, path, PN_MAX_DEPTH);

	ASSERT_EQ(rc, 1);
	ASSERT_EQ(rec.count, 3); // root, gain, mute
	ASSERT_STR(rec.names[0], "root");
	ASSERT_STR(rec.names[1], "gain");
	ASSERT_STR(rec.names[2], "mute");
	return 0;
}

// === End-to-end test helpers ===

typedef struct {
	uint8_t buf[2048];
	uint32_t len;
} capture_t;

static int capture_send(const uint8_t *buf, uint32_t len, void *ctx) {
	capture_t *cap = ctx;
	if (cap->len + len > sizeof(cap->buf)) return -1;
	memcpy(cap->buf + cap->len, buf, len);
	cap->len += len;
	return 0;
}

#define MAX_UPDATES 16

typedef struct {
	struct {
		char path[PN_MAX_PATH];
		pn_value_t value;
		pn_type_t type;
		char str_buf[PN_MAX_STRING];
	} items[MAX_UPDATES];
	int count;
} update_rx_t;

static void capture_update(const char *path, const pn_value_t *val,
							pn_type_t type, void *ctx) {
	update_rx_t *rx = ctx;
	if (rx->count >= MAX_UPDATES) return;
	int i = rx->count++;
	strncpy(rx->items[i].path, path, PN_MAX_PATH - 1);
	rx->items[i].path[PN_MAX_PATH - 1] = '\0';
	rx->items[i].value = *val;
	rx->items[i].type = type;
	if (type == PN_STRING && val->s) {
		strncpy(rx->items[i].str_buf, val->s, PN_MAX_STRING - 1);
		rx->items[i].str_buf[PN_MAX_STRING - 1] = '\0';
		rx->items[i].value.s = rx->items[i].str_buf;
	}
}

typedef struct {
	uint32_t hash;
	char root[PN_MAX_PATH];
	int count;
} schema_rx_t;

static void capture_schema(uint32_t hash, const char *root, void *ctx) {
	schema_rx_t *rx = ctx;
	rx->hash = hash;
	strncpy(rx->root, root, PN_MAX_PATH - 1);
	rx->root[PN_MAX_PATH - 1] = '\0';
	rx->count++;
}

typedef struct {
	uint8_t buf[4096];
	uint32_t len;
	int chunks;
	int last_count;  // chunks that had last set
	int last_index;  // 1-based index of the chunk that had last set
} manifest_rx_t;

static void capture_manifest(const uint8_t *data, uint32_t len, bool last, void *ctx) {
	manifest_rx_t *rx = ctx;
	if (rx->len + len <= sizeof(rx->buf)) {
		memcpy(rx->buf + rx->len, data, len);
		rx->len += len;
	}
	rx->chunks++;
	if (last) {
		rx->last_count++;
		rx->last_index = rx->chunks;
	}
}

typedef struct {
	test_tree_t tree;
	capture_t srv_tx, cli_tx;
	update_rx_t updates;
	schema_rx_t schema;
	manifest_rx_t manifest;
	pn_server_t *server;
	pn_client_t *client;
} e2e_t;

static uint32_t e2e_srv_size(void) {
	return pn_server_alloc_size(NULL);
}

static uint32_t e2e_cli_size(void) {
	return pn_client_alloc_size(NULL);
}

static void e2e_init_root(e2e_t *e, uint8_t *srv_mem, uint8_t *cli_mem, pn_param_t *root) {
	pn_server_config_t srv_cfg = {
		.root = root,
		.send = capture_send,
		.send_ctx = &e->srv_tx,
	};
	e->server = (pn_server_t *)srv_mem;
	pn_server_init(e->server, &srv_cfg);
	pn_server_start(e->server);

	pn_client_config_t cli_cfg = {
		.send = capture_send,
		.send_ctx = &e->cli_tx,
		.on_update = capture_update,
		.on_update_ctx = &e->updates,
		.on_schema = capture_schema,
		.on_schema_ctx = &e->schema,
		.on_manifest = capture_manifest,
		.on_manifest_ctx = &e->manifest,
	};
	e->client = (pn_client_t *)cli_mem;
	pn_client_init(e->client, &cli_cfg);
}

static void e2e_init(e2e_t *e, uint8_t *srv_mem, uint8_t *cli_mem) {
	memset(e, 0, sizeof(*e));
	test_tree_init(&e->tree);
	e2e_init_root(e, srv_mem, cli_mem, &e->tree.root);
}

// Client -> Server -> Client roundtrip
static void e2e_roundtrip(e2e_t *e) {
	pn_server_feed(e->server, e->cli_tx.buf, e->cli_tx.len);
	e->cli_tx.len = 0;
	pn_client_feed(e->client, e->srv_tx.buf, e->srv_tx.len);
	e->srv_tx.len = 0;
}

#define E2E_SETUP(name) \
	e2e_t name; \
	alignas(max_align_t) uint8_t name##_sm[e2e_srv_size()]; \
	alignas(max_align_t) uint8_t name##_cm[e2e_cli_size()]; \
	e2e_init(&name, name##_sm, name##_cm)

// === End-to-end tests ===

static int test_e2e_set_float(void) {
	E2E_SETUP(e);
	pn_client_set_float(e.client, "root/gain", 3.0f);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 1);
	ASSERT_STR(e.updates.items[0].path, "root/gain");
	ASSERT_EQ(e.updates.items[0].type, PN_FLOAT);
	ASSERT_FLOAT(e.updates.items[0].value.f, 3.0f);
	ASSERT_FLOAT(e.tree.gain, 3.0f);
	return 0;
}

static int test_e2e_set_clamp(void) {
	E2E_SETUP(e);
	pn_client_set_float(e.client, "root/gain", 100.0f);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 1);
	ASSERT_FLOAT(e.updates.items[0].value.f, 6.0f); // clamped to max
	ASSERT_FLOAT(e.tree.gain, 6.0f);
	return 0;
}

static int test_e2e_get_float(void) {
	E2E_SETUP(e);
	e.tree.gain = -6.0f;
	pn_client_get(e.client, "root/gain");
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 1);
	ASSERT_STR(e.updates.items[0].path, "root/gain");
	ASSERT_EQ(e.updates.items[0].type, PN_FLOAT);
	ASSERT_FLOAT(e.updates.items[0].value.f, -6.0f);
	return 0;
}

static int test_e2e_get_group(void) {
	E2E_SETUP(e);
	e.tree.level = 75;
	e.tree.pan = 0.5f;
	pn_client_get(e.client, "root/sub");
	e2e_roundtrip(&e);
	// sub has 2 leaf params: level, pan
	ASSERT_EQ(e.updates.count, 2);
	ASSERT_STR(e.updates.items[0].path, "root/sub/level");
	ASSERT_EQ(e.updates.items[0].type, PN_INT);
	ASSERT_EQ(e.updates.items[0].value.i, 75);
	ASSERT_STR(e.updates.items[1].path, "root/sub/pan");
	ASSERT_EQ(e.updates.items[1].type, PN_FLOAT);
	ASSERT_FLOAT(e.updates.items[1].value.f, 0.5f);
	return 0;
}

static int test_e2e_get_group_trailing_slash(void) {
	E2E_SETUP(e);
	e.tree.level = 42;
	e.tree.pan = -0.5f;
	pn_client_get(e.client, "root/sub/");
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 2);
	// Paths must not have double slashes
	ASSERT_STR(e.updates.items[0].path, "root/sub/level");
	ASSERT_STR(e.updates.items[1].path, "root/sub/pan");
	return 0;
}

static int test_e2e_action(void) {
	E2E_SETUP(e);
	action_fired = 0;
	pn_client_action(e.client, "root/reset");
	pn_server_feed(e.server, e.cli_tx.buf, e.cli_tx.len);
	ASSERT_EQ(action_fired, 1);
	// No update sent for actions
	ASSERT_EQ(e.srv_tx.len, 0);
	return 0;
}

static int test_e2e_coerce_int_to_float(void) {
	E2E_SETUP(e);
	pn_client_set_int(e.client, "root/gain", 3);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 1);
	ASSERT_EQ(e.updates.items[0].type, PN_FLOAT);
	ASSERT_FLOAT(e.updates.items[0].value.f, 3.0f);
	ASSERT_FLOAT(e.tree.gain, 3.0f);
	return 0;
}

static int test_e2e_wrong_root(void) {
	E2E_SETUP(e);
	pn_client_set_float(e.client, "other/gain", 3.0f);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 0);
	ASSERT_FLOAT(e.tree.gain, 0.0f);
	return 0;
}

static int test_e2e_set_bool(void) {
	E2E_SETUP(e);
	pn_client_set_bool(e.client, "root/mute", true);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 1);
	ASSERT_STR(e.updates.items[0].path, "root/mute");
	ASSERT_EQ(e.updates.items[0].type, PN_BOOL);
	ASSERT(e.updates.items[0].value.b == true);
	ASSERT(e.tree.mute == true);
	return 0;
}

static int test_e2e_set_int(void) {
	E2E_SETUP(e);
	pn_client_set_int(e.client, "root/sub/level", 75);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 1);
	ASSERT_STR(e.updates.items[0].path, "root/sub/level");
	ASSERT_EQ(e.updates.items[0].type, PN_INT);
	ASSERT_EQ(e.updates.items[0].value.i, 75);
	ASSERT_EQ(e.tree.level, 75);
	return 0;
}

static int test_e2e_set_enum(void) {
	E2E_SETUP(e);
	pn_client_set_enum(e.client, "root/input", 1);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 1);
	ASSERT_STR(e.updates.items[0].path, "root/input");
	ASSERT_EQ(e.updates.items[0].type, PN_ENUM);
	ASSERT_EQ(e.updates.items[0].value.e, 1);
	ASSERT_EQ(e.tree.input, 1);
	return 0;
}

static int test_e2e_set_string(void) {
	E2E_SETUP(e);
	pn_client_set_string(e.client, "root/name", "ch1");
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 1);
	ASSERT_STR(e.updates.items[0].path, "root/name");
	ASSERT_EQ(e.updates.items[0].type, PN_STRING);
	ASSERT_STR(e.updates.items[0].value.s, "ch1");
	ASSERT_STR(e.tree.name, "ch1");
	return 0;
}

static int test_e2e_get_root(void) {
	E2E_SETUP(e);
	e.tree.gain = -6.0f;
	e.tree.mute = true;
	e.tree.input = 1;
	strncpy(e.tree.name, "master", 31);
	e.tree.level = 75;
	e.tree.pan = 0.5f;
	pn_client_get(e.client, "root");
	e2e_roundtrip(&e);
	// Should get all leaf params (gain, mute, input, name, level, pan)
	// but NOT reset (action) or sub (group)
	ASSERT_EQ(e.updates.count, 6);
	ASSERT_STR(e.updates.items[0].path, "root/gain");
	ASSERT_EQ(e.updates.items[0].type, PN_FLOAT);
	ASSERT_FLOAT(e.updates.items[0].value.f, -6.0f);
	ASSERT_STR(e.updates.items[1].path, "root/mute");
	ASSERT_EQ(e.updates.items[1].type, PN_BOOL);
	ASSERT(e.updates.items[1].value.b == true);
	ASSERT_STR(e.updates.items[2].path, "root/input");
	ASSERT_EQ(e.updates.items[2].type, PN_ENUM);
	ASSERT_EQ(e.updates.items[2].value.e, 1);
	ASSERT_STR(e.updates.items[3].path, "root/name");
	ASSERT_EQ(e.updates.items[3].type, PN_STRING);
	ASSERT_STR(e.updates.items[3].value.s, "master");
	// depth-2 paths (sub/level, sub/pan)
	ASSERT_STR(e.updates.items[4].path, "root/sub/level");
	ASSERT_EQ(e.updates.items[4].type, PN_INT);
	ASSERT_EQ(e.updates.items[4].value.i, 75);
	ASSERT_STR(e.updates.items[5].path, "root/sub/pan");
	ASSERT_EQ(e.updates.items[5].type, PN_FLOAT);
	ASSERT_FLOAT(e.updates.items[5].value.f, 0.5f);
	return 0;
}

static int test_e2e_coerce_float_to_int(void) {
	E2E_SETUP(e);
	pn_client_set_float(e.client, "root/sub/level", 42.7f);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 1);
	ASSERT_EQ(e.updates.items[0].type, PN_INT);
	ASSERT_EQ(e.updates.items[0].value.i, 42);
	ASSERT_EQ(e.tree.level, 42);
	return 0;
}

static int test_e2e_coerce_int_to_bool(void) {
	E2E_SETUP(e);
	pn_client_set_int(e.client, "root/mute", 1);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 1);
	ASSERT_EQ(e.updates.items[0].type, PN_BOOL);
	ASSERT(e.updates.items[0].value.b == true);
	ASSERT(e.tree.mute == true);
	return 0;
}

static int test_e2e_coerce_int_to_enum(void) {
	E2E_SETUP(e);
	pn_client_set_int(e.client, "root/input", 1);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 1);
	ASSERT_EQ(e.updates.items[0].type, PN_ENUM);
	ASSERT_EQ(e.updates.items[0].value.e, 1);
	ASSERT_EQ(e.tree.input, 1);
	return 0;
}

static int test_e2e_unsupported_coercion(void) {
	E2E_SETUP(e);
	// String -> float: no coercion rule, silently rejected
	pn_client_set_string(e.client, "root/gain", "hello");
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 0);
	ASSERT_FLOAT(e.tree.gain, 0.0f);
	return 0;
}

static int test_e2e_sequential_ops(void) {
	E2E_SETUP(e);
	// First: set gain
	pn_client_set_float(e.client, "root/gain", 3.0f);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 1);
	ASSERT_FLOAT(e.tree.gain, 3.0f);

	// Second: set mute (reuses COBS decoder and encode buffer)
	pn_client_set_bool(e.client, "root/mute", true);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 2);
	ASSERT(e.tree.mute == true);

	// Third: get gain (confirms first set persisted)
	pn_client_get(e.client, "root/gain");
	e2e_roundtrip(&e);
	ASSERT_EQ(e.updates.count, 3);
	ASSERT_FLOAT(e.updates.items[2].value.f, 3.0f);
	return 0;
}

static int test_e2e_notify(void) {
	E2E_SETUP(e);
	// Modify param value directly on the server side
	e.tree.gain = -12.0f;
	// Notify — server builds path from bind.parent and sends Update
	ASSERT_EQ(pn_param_notify(&e.tree.nodes[0]), 0);
	// Feed server output to client
	pn_client_feed(e.client, e.srv_tx.buf, e.srv_tx.len);
	e.srv_tx.len = 0;
	ASSERT_EQ(e.updates.count, 1);
	ASSERT_STR(e.updates.items[0].path, "root/gain");
	ASSERT_EQ(e.updates.items[0].type, PN_FLOAT);
	ASSERT_FLOAT(e.updates.items[0].value.f, -12.0f);
	return 0;
}

static int test_e2e_notify_nested(void) {
	E2E_SETUP(e);
	// Modify a param nested under a subgroup
	e.tree.level = 42;
	ASSERT_EQ(pn_param_notify(&e.tree.sub_nodes[0]), 0);
	pn_client_feed(e.client, e.srv_tx.buf, e.srv_tx.len);
	e.srv_tx.len = 0;
	ASSERT_EQ(e.updates.count, 1);
	ASSERT_STR(e.updates.items[0].path, "root/sub/level");
	ASSERT_EQ(e.updates.items[0].type, PN_INT);
	ASSERT_EQ(e.updates.items[0].value.i, 42);
	return 0;
}

// === Manifest test helpers ===

typedef struct {
	char buf[2048];
	uint32_t len;
	int calls;
	int fail_at_call;  // 0: never fail
	int fail_rc;
} manifest_sink_t;

static int manifest_sink(const uint8_t *buf, uint32_t len, void *ctx) {
	manifest_sink_t *s = ctx;
	s->calls++;
	if (s->fail_at_call && s->calls >= s->fail_at_call)
		return s->fail_rc;
	if (s->len + len >= sizeof(s->buf)) return -1;
	memcpy(s->buf + s->len, buf, len);
	s->len += len;
	s->buf[s->len] = '\0';
	return 0;
}

// The body of a written manifest: everything after the header line.
static const char *manifest_body(const manifest_sink_t *s) {
	const char *nl = strchr(s->buf, '\n');
	return nl ? nl + 1 : s->buf;
}

// The manifest of the amp schema (amp_tree.h), as MANIFEST.md shows it.
static const char amp_manifest[] =
	"# pn manifest v1 amp fnv1a32=76a2b061\n"
	"amp/power bool\n"
	"amp/volume int:% 0 100 1\n"
	"amp/eq/bass float:dB 0xc1400000 0x41400000 0x3f000000\n"
	"amp/eq/treble float:dB 0xc1400000 0x41400000 0x3f000000\n"
	"amp/input enum mic|line|aux\n"
	"amp/name string 32\n"
	"amp/level float 0x00000000 0x3f800000 0x00000000 ro\n"
	"amp/reset action\n";

#define AMP_HASH 0x76a2b061u

// === Hash tests ===

// FNV-1a 32-bit matches the reference test vectors.
static int test_fnv1a32_vectors(void) {
	ASSERT_EQ(pn_fnv1a32(PN_FNV1A32_INIT, NULL, 0), 0x811c9dc5u);
	ASSERT_EQ(pn_fnv1a32(PN_FNV1A32_INIT, (const uint8_t *)"a", 1), 0xe40c292cu);
	ASSERT_EQ(pn_fnv1a32(PN_FNV1A32_INIT, (const uint8_t *)"foobar", 6), 0xbf9cf968u);
	return 0;
}

// Hashing data in pieces gives the same result as hashing it at once.
static int test_fnv1a32_incremental(void) {
	uint32_t h = pn_fnv1a32(PN_FNV1A32_INIT, (const uint8_t *)"foo", 3);
	h = pn_fnv1a32(h, (const uint8_t *)"bar", 3);
	ASSERT_EQ(h, 0xbf9cf968u);
	return 0;
}

// === Manifest tests ===

// The manifest of the MANIFEST.md example schema is byte-identical to the text
// shown there, and its schema hash is the declared hash.
static int test_manifest_example(void) {
	amp_tree_t t;
	amp_tree_build(&t);

	manifest_sink_t s = {0};
	ASSERT_EQ(pn_manifest_write(&t.root, manifest_sink, &s), 0);
	ASSERT_STR(s.buf, amp_manifest);

	uint32_t hash = 0;
	ASSERT_EQ(pn_schema_hash(&t.root, &hash), 0);
	ASSERT_EQ(hash, AMP_HASH);
	return 0;
}

// The schema hash covers exactly the bytes written after the header.
static int test_manifest_hash_covers_body(void) {
	test_tree_t t;
	test_tree_init(&t);

	manifest_sink_t s = {0};
	ASSERT_EQ(pn_manifest_write(&t.root, manifest_sink, &s), 0);
	const char *body = manifest_body(&s);
	uint32_t written = pn_fnv1a32(PN_FNV1A32_INIT, (const uint8_t *)body, (uint32_t)strlen(body));

	uint32_t hash = 0;
	ASSERT_EQ(pn_schema_hash(&t.root, &hash), 0);
	ASSERT_EQ(hash, written);
	return 0;
}

// Entries follow declared order, depth first, and groups produce no entry.
static int test_manifest_order(void) {
	test_tree_t t;
	test_tree_init(&t);

	manifest_sink_t s = {0};
	ASSERT_EQ(pn_manifest_write(&t.root, manifest_sink, &s), 0);
	ASSERT_STR(manifest_body(&s),
		"root/gain float 0xc2900000 0x40c00000 0x3dcccccd\n"
		"root/mute bool\n"
		"root/input enum mic|line\n"
		"root/name string 32\n"
		"root/reset action\n"
		"root/sub/level int 0 100 1\n"
		"root/sub/pan float 0xbf800000 0x3f800000 0x3c23d70a\n");
	return 0;
}

// An empty group changes neither the manifest nor the hash.
static int test_manifest_empty_group_invisible(void) {
	pn_param_t with_nodes[3];
	with_nodes[0] = pn_bool("a", NULL, NULL, NULL);
	with_nodes[1] = pn_group("empty", NULL, 0);
	with_nodes[2] = pn_bool("b", NULL, NULL, NULL);
	pn_param_t with = pn_group("r", with_nodes, 3);

	pn_param_t without_nodes[2];
	without_nodes[0] = with_nodes[0];
	without_nodes[1] = with_nodes[2];
	pn_param_t without = pn_group("r", without_nodes, 2);

	uint32_t h_with = 0, h_without = 0;
	ASSERT_EQ(pn_schema_hash(&with, &h_with), 0);
	ASSERT_EQ(pn_schema_hash(&without, &h_without), 0);
	ASSERT_EQ(h_with, h_without);
	return 0;
}

// A schema with no leaves has an empty body and hashes to the FNV offset basis.
static int test_manifest_no_leaves(void) {
	pn_param_t root = pn_group("r", NULL, 0);
	manifest_sink_t s = {0};
	ASSERT_EQ(pn_manifest_write(&root, manifest_sink, &s), 0);
	ASSERT_STR(s.buf, "# pn manifest v1 r fnv1a32=811c9dc5\n");
	return 0;
}

// Float constraints are written as their binary32 bit pattern, verbatim:
// the sign of zero and a NaN payload survive.
static int test_manifest_float_bits(void) {
	uint32_t nan_bits = 0x7fc00123u;
	float nan;
	memcpy(&nan, &nan_bits, sizeof(nan));

	pn_param_t leaf = pn_float("f", NULL, NULL, NULL, -0.0f, nan, 0.0f);
	pn_param_t root = pn_group("r", &leaf, 1);
	manifest_sink_t s = {0};
	ASSERT_EQ(pn_manifest_write(&root, manifest_sink, &s), 0);
	ASSERT_STR(manifest_body(&s), "r/f float 0x80000000 0x7fc00123 0x00000000\n");
	return 0;
}

// Integer constraints are written in decimal across the whole int32 range.
static int test_manifest_int_range(void) {
	pn_param_t leaf = pn_int("i", NULL, NULL, NULL, INT32_MIN, INT32_MAX, -1);
	pn_param_t root = pn_group("r", &leaf, 1);
	manifest_sink_t s = {0};
	ASSERT_EQ(pn_manifest_write(&root, manifest_sink, &s), 0);
	ASSERT_STR(manifest_body(&s), "r/i int -2147483648 2147483647 -1\n");
	return 0;
}

// Names and labels are written as their UTF-8 bytes, unchanged.
static int test_manifest_utf8(void) {
	static const char *labels[] = { "µs", "ms" };
	pn_param_t leaf = pn_enum("Ω", NULL, NULL, NULL, labels, 2);
	pn_param_t root = pn_group("r", &leaf, 1);
	manifest_sink_t s = {0};
	ASSERT_EQ(pn_manifest_write(&root, manifest_sink, &s), 0);
	ASSERT_STR(manifest_body(&s), "r/Ω enum µs|ms\n");
	return 0;
}

// A sink error aborts the write, is returned unchanged, and the sink is not
// called again.
static int test_manifest_sink_error(void) {
	amp_tree_t t;
	amp_tree_build(&t);
	manifest_sink_t s = { .fail_at_call = 5, .fail_rc = 7 };
	ASSERT_EQ(pn_manifest_write(&t.root, manifest_sink, &s), 7);
	ASSERT_EQ(s.calls, 5);
	return 0;
}

// Neither the hash nor the manifest is produced for a schema that does not
// validate, and the sink is never called.
static int test_manifest_rejects_invalid(void) {
	pn_param_t leaf = pn_bool("bad name", NULL, NULL, NULL);
	pn_param_t root = pn_group("r", &leaf, 1);

	uint32_t hash = 0;
	ASSERT_EQ(pn_schema_hash(&root, &hash), -1);

	manifest_sink_t s = {0};
	ASSERT_EQ(pn_manifest_write(&root, manifest_sink, &s), -1);
	ASSERT_EQ(s.calls, 0);
	return 0;
}

// === Schema validation tests ===

// A valid schema validates, reporting no offender.
static int test_validate_ok(void) {
	amp_tree_t t;
	amp_tree_build(&t);
	const pn_param_t *bad = &t.root;
	ASSERT_EQ(pn_schema_validate(&t.root, &bad), PN_SCHEMA_OK);
	ASSERT(bad == NULL);
	return 0;
}

// A NULL root is rejected without an offender.
static int test_validate_no_root(void) {
	const pn_param_t *bad = (const pn_param_t *)1;
	ASSERT_EQ(pn_schema_validate(NULL, &bad), PN_SCHEMA_NO_ROOT);
	ASSERT(bad == NULL);
	return 0;
}

// Every forbidden byte in a name is rejected, and the offending node is reported.
static int test_validate_bad_name(void) {
	static const char *bad_names[] = { "", "a b", "a/b", "a#b", "a|b", "a\tb", "\x7f", "a\x01" };
	for (size_t i = 0; i < sizeof(bad_names) / sizeof(bad_names[0]); i++) {
		pn_param_t leaf = pn_bool(bad_names[i], NULL, NULL, NULL);
		pn_param_t root = pn_group("r", &leaf, 1);
		const pn_param_t *bad = NULL;
		ASSERT_EQ(pn_schema_validate(&root, &bad), PN_SCHEMA_BAD_NAME);
		ASSERT(bad == &leaf);
	}

	pn_param_t unnamed = pn_bool(NULL, NULL, NULL, NULL);
	pn_param_t root = pn_group("r", &unnamed, 1);
	ASSERT_EQ(pn_schema_validate(&root, NULL), PN_SCHEMA_BAD_NAME);
	return 0;
}

// The root name follows the same rules as every other name.
static int test_validate_bad_root_name(void) {
	pn_param_t root = pn_group("my root", NULL, 0);
	const pn_param_t *bad = NULL;
	ASSERT_EQ(pn_schema_validate(&root, &bad), PN_SCHEMA_BAD_NAME);
	ASSERT(bad == &root);
	return 0;
}

// Enum labels follow the name rules, and an enum needs at least one label.
static int test_validate_labels(void) {
	static const char *bad_labels[] = { "mic", "line in" };
	pn_param_t leaf = pn_enum("e", NULL, NULL, NULL, bad_labels, 2);
	pn_param_t root = pn_group("r", &leaf, 1);
	ASSERT_EQ(pn_schema_validate(&root, NULL), PN_SCHEMA_BAD_LABEL);

	leaf = pn_enum("e", NULL, NULL, NULL, NULL, 1);
	ASSERT_EQ(pn_schema_validate(&root, NULL), PN_SCHEMA_BAD_LABEL);

	leaf = pn_enum("e", NULL, NULL, NULL, NULL, 0);
	ASSERT_EQ(pn_schema_validate(&root, NULL), PN_SCHEMA_EMPTY_ENUM);
	return 0;
}

// A unit belongs to an int or a float, follows the name rules, and holds no
// colon; the manifest writes it after the type.
static int test_validate_unit(void) {
	pn_param_t leaf = pn_unit(pn_float("f", NULL, NULL, NULL, 0.0f, 1.0f, 0.0f), "dB");
	pn_param_t root = pn_group("r", &leaf, 1);
	ASSERT_EQ(pn_schema_validate(&root, NULL), PN_SCHEMA_OK);

	manifest_sink_t s = {0};
	ASSERT_EQ(pn_manifest_write(&root, manifest_sink, &s), 0);
	ASSERT_STR(manifest_body(&s), "r/f float:dB 0x00000000 0x3f800000 0x00000000\n");

	leaf = pn_unit(pn_bool("b", NULL, NULL, NULL), "dB");
	const pn_param_t *bad = NULL;
	ASSERT_EQ(pn_schema_validate(&root, &bad), PN_SCHEMA_BAD_UNIT);
	ASSERT(bad == &leaf);

	leaf = pn_unit(pn_int("i", NULL, NULL, NULL, 0, 1, 1), "a:b");
	ASSERT_EQ(pn_schema_validate(&root, NULL), PN_SCHEMA_BAD_UNIT);
	leaf = pn_unit(pn_int("i", NULL, NULL, NULL, 0, 1, 1), "");
	ASSERT_EQ(pn_schema_validate(&root, NULL), PN_SCHEMA_BAD_UNIT);
	leaf = pn_unit(pn_int("i", NULL, NULL, NULL, 0, 1, 1), "per cent");
	ASSERT_EQ(pn_schema_validate(&root, NULL), PN_SCHEMA_BAD_UNIT);
	ASSERT_STR(pn_schema_result_name(PN_SCHEMA_BAD_UNIT), "bad unit");
	return 0;
}

// Siblings must have distinct names; the later duplicate is reported.
static int test_validate_duplicate(void) {
	pn_param_t nodes[3];
	nodes[0] = pn_bool("a", NULL, NULL, NULL);
	nodes[1] = pn_bool("b", NULL, NULL, NULL);
	nodes[2] = pn_bool("a", NULL, NULL, NULL);
	pn_param_t root = pn_group("r", nodes, 3);
	const pn_param_t *bad = NULL;
	ASSERT_EQ(pn_schema_validate(&root, &bad), PN_SCHEMA_DUPLICATE_NAME);
	ASSERT(bad == &nodes[2]);
	return 0;
}

// Only a group may have children.
static int test_validate_not_group(void) {
	pn_param_t child = pn_bool("c", NULL, NULL, NULL);
	pn_param_t leaf = pn_bool("leaf", NULL, NULL, NULL);
	leaf.children = &child;
	leaf.num_children = 1;
	pn_param_t root = pn_group("r", &leaf, 1);
	const pn_param_t *bad = NULL;
	ASSERT_EQ(pn_schema_validate(&root, &bad), PN_SCHEMA_NOT_GROUP);
	ASSERT(bad == &leaf);
	return 0;
}

// A node whose type is not a pn_type_t value is rejected.
static int test_validate_bad_type(void) {
	pn_param_t leaf = pn_bool("leaf", NULL, NULL, NULL);
	leaf.node.type = (pn_type_t)99;
	pn_param_t root = pn_group("r", &leaf, 1);
	ASSERT_EQ(pn_schema_validate(&root, NULL), PN_SCHEMA_BAD_TYPE);
	return 0;
}

// A tree may have at most PN_MAX_DEPTH levels, the root included.
static int test_validate_too_deep(void) {
	pn_param_t chain[PN_MAX_DEPTH + 1];
	chain[PN_MAX_DEPTH] = pn_bool("leaf", NULL, NULL, NULL);
	for (int i = PN_MAX_DEPTH - 1; i >= 0; i--)
		chain[i] = pn_group("g", &chain[i + 1], 1);

	// chain[0] is the root at depth 0, so the leaf sits at depth PN_MAX_DEPTH
	ASSERT_EQ(pn_schema_validate(&chain[0], NULL), PN_SCHEMA_TOO_DEEP);

	// One level shallower fits
	chain[PN_MAX_DEPTH - 1] = pn_bool("leaf", NULL, NULL, NULL);
	ASSERT_EQ(pn_schema_validate(&chain[0], NULL), PN_SCHEMA_OK);
	return 0;
}

// A path must fit in PN_MAX_PATH bytes including its terminator.
static int test_validate_path_too_long(void) {
	static char name[257];
	memset(name, 'x', 256);
	name[256] = '\0';

	pn_param_t fits = pn_group(name + 1, NULL, 0);   // 255 bytes
	ASSERT_EQ(pn_schema_validate(&fits, NULL), PN_SCHEMA_OK);

	pn_param_t too_long = pn_group(name, NULL, 0);   // 256 bytes
	ASSERT_EQ(pn_schema_validate(&too_long, NULL), PN_SCHEMA_PATH_TOO_LONG);

	// Short names whose joined path is too long: 3 x 100 bytes + 2 separators
	name[100] = '\0';
	pn_param_t leaf = pn_bool(name, NULL, NULL, NULL);
	pn_param_t mid = pn_group(name, &leaf, 1);
	pn_param_t root = pn_group(name, &mid, 1);
	const pn_param_t *bad = NULL;
	ASSERT_EQ(pn_schema_validate(&root, &bad), PN_SCHEMA_PATH_TOO_LONG);
	ASSERT(bad == &leaf);
	return 0;
}

// Every validation result has a short description.
static int test_schema_result_name(void) {
	ASSERT_STR(pn_schema_result_name(PN_SCHEMA_OK), "ok");
	ASSERT_STR(pn_schema_result_name(PN_SCHEMA_DUPLICATE_NAME), "duplicate name");
	ASSERT_STR(pn_schema_result_name(PN_SCHEMA_PATH_TOO_LONG), "path too long");
	ASSERT_STR(pn_schema_result_name((pn_schema_result_t)99), "unknown");
	return 0;
}

// === Server start tests ===

// A server refuses to start over a schema that does not validate.
static int test_server_start_rejects_invalid(void) {
	pn_param_t leaf = pn_bool("bad name", NULL, NULL, NULL);
	pn_param_t root = pn_group("r", &leaf, 1);
	pn_server_config_t cfg = { .root = &root };
	alignas(max_align_t) uint8_t mem[e2e_srv_size()];
	pn_server_t *server = (pn_server_t *)mem;
	pn_server_init(server, &cfg);
	ASSERT_EQ(pn_server_start(server), -1);
	return 0;
}

// A started server reports the schema hash of its tree.
static int test_server_schema_hash(void) {
	amp_tree_t t;
	amp_tree_build(&t);
	pn_server_config_t cfg = { .root = &t.root };
	alignas(max_align_t) uint8_t mem[e2e_srv_size()];
	pn_server_t *server = (pn_server_t *)mem;
	pn_server_init(server, &cfg);
	ASSERT_EQ(pn_server_start(server), 0);
	ASSERT_EQ(pn_server_schema_hash(server), AMP_HASH);
	return 0;
}

// === Schema and manifest over the wire ===

// A schema request addressed to the server's root is answered with the
// server's schema hash and root name.
static int test_e2e_schema_get(void) {
	E2E_SETUP(e);
	ASSERT_EQ(pn_client_get_schema(e.client, "root"), 0);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.schema.count, 1);
	ASSERT_EQ(e.schema.hash, pn_server_schema_hash(e.server));
	ASSERT_STR(e.schema.root, "root");
	return 0;
}

// A schema request with no root is answered by every server.
static int test_e2e_schema_get_any(void) {
	E2E_SETUP(e);
	ASSERT_EQ(pn_client_get_schema(e.client, NULL), 0);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.schema.count, 1);
	ASSERT_STR(e.schema.root, "root");

	ASSERT_EQ(pn_client_get_schema(e.client, ""), 0);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.schema.count, 2);
	return 0;
}

// A schema request addressed to another root is ignored.
static int test_e2e_schema_get_other_root(void) {
	E2E_SETUP(e);
	ASSERT_EQ(pn_client_get_schema(e.client, "other"), 0);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.schema.count, 0);
	return 0;
}

// A manifest request is answered with the server's manifest, byte for byte,
// in a single chunk when it fits, with last set.
static int test_e2e_manifest_get(void) {
	E2E_SETUP(e);
	ASSERT_EQ(pn_client_get_manifest(e.client, "root"), 0);
	e2e_roundtrip(&e);

	manifest_sink_t direct = {0};
	ASSERT_EQ(pn_manifest_write(&e.tree.root, manifest_sink, &direct), 0);

	ASSERT_EQ(e.manifest.chunks, 1);
	ASSERT_EQ(e.manifest.last_count, 1);
	ASSERT_EQ(e.manifest.len, direct.len);
	ASSERT_MEM(e.manifest.buf, direct.buf, direct.len);
	return 0;
}

// A manifest request addressed to another root is ignored, and a request
// without a root is refused by the client.
static int test_e2e_manifest_get_other_root(void) {
	E2E_SETUP(e);
	ASSERT_EQ(pn_client_get_manifest(e.client, NULL), -1);
	ASSERT_EQ(pn_client_get_manifest(e.client, "other"), 0);
	e2e_roundtrip(&e);
	ASSERT_EQ(e.manifest.chunks, 0);
	return 0;
}

// A manifest longer than one chunk arrives in order across several full
// chunks, with last set only on the final one, which is never empty.
static int test_e2e_manifest_chunked(void) {
	// 24 float entries of 47 bytes plus the header: three chunks
	enum { N = 24 };
	static char names[N][4];
	static pn_param_t leaves[N];
	for (int i = 0; i < N; i++) {
		snprintf(names[i], sizeof(names[i]), "p%02d", i);
		leaves[i] = pn_float(names[i], NULL, NULL, NULL, -1.0f, 1.0f, 0.0f);
	}
	pn_param_t root = pn_group("big", leaves, N);

	e2e_t e;
	memset(&e, 0, sizeof(e));
	alignas(max_align_t) uint8_t sm[e2e_srv_size()];
	alignas(max_align_t) uint8_t cm[e2e_cli_size()];
	e2e_init_root(&e, sm, cm, &root);

	ASSERT_EQ(pn_client_get_manifest(e.client, "big"), 0);
	e2e_roundtrip(&e);

	manifest_sink_t direct = {0};
	ASSERT_EQ(pn_manifest_write(&root, manifest_sink, &direct), 0);
	ASSERT(direct.len > 2 * 512);

	ASSERT_EQ(e.manifest.chunks, 3);
	ASSERT_EQ(e.manifest.last_count, 1);
	ASSERT_EQ(e.manifest.last_index, 3);
	ASSERT_EQ(e.manifest.len, direct.len);
	ASSERT_MEM(e.manifest.buf, direct.buf, direct.len);
	return 0;
}

// === Main ===

int main(void) {
	printf("crc:\n");
	TEST(test_crc_empty);
	TEST(test_crc_known_vector);
	TEST(test_crc_single_byte);
	TEST(test_crc_detects_change);
	TEST(test_crc_detects_reorder);

	printf("cobs:\n");
	TEST(test_cobs_roundtrip_basic);
	TEST(test_cobs_roundtrip_empty);
	TEST(test_cobs_roundtrip_single_zero);
	TEST(test_cobs_roundtrip_all_zeros);
	TEST(test_cobs_roundtrip_no_zeros);
	TEST(test_cobs_roundtrip_254_bytes);
	TEST(test_cobs_roundtrip_255_bytes);
	TEST(test_cobs_roundtrip_zeros_at_boundaries);
	TEST(test_cobs_multiple_frames);
	TEST(test_cobs_consecutive_delimiters);
	TEST(test_cobs_corrupted_crc);
	TEST(test_cobs_encode_too_large);
	TEST(test_cobs_encode_buffer_too_small);
	TEST(test_cobs_byte_at_a_time);
	TEST(test_cobs_overflow_discards);

	printf("param find:\n");
	TEST(test_find_root);
	TEST(test_find_leaf);
	TEST(test_find_nested);
	TEST(test_find_wrong_root);
	TEST(test_find_nonexistent);
	TEST(test_find_slashes);
	TEST(test_find_null);

	printf("param set/get:\n");
	TEST(test_setget_float);
	TEST(test_setget_int);
	TEST(test_setget_bool);
	TEST(test_setget_enum);
	TEST(test_setget_string);
	TEST(test_clamp_float);
	TEST(test_clamp_int);
	TEST(test_clamp_enum);
	TEST(test_action);
	TEST(test_set_no_setter);
	TEST(test_get_no_getter);
	TEST(test_enum_zero_count);
	TEST(test_set_group_rejects);

	printf("param walk:\n");
	TEST(test_walk_visits_all);
	TEST(test_walk_early_stop);

	printf("end-to-end:\n");
	TEST(test_e2e_set_float);
	TEST(test_e2e_set_clamp);
	TEST(test_e2e_get_float);
	TEST(test_e2e_get_group);
	TEST(test_e2e_get_group_trailing_slash);
	TEST(test_e2e_action);
	TEST(test_e2e_coerce_int_to_float);
	TEST(test_e2e_wrong_root);
	TEST(test_e2e_set_bool);
	TEST(test_e2e_set_int);
	TEST(test_e2e_set_enum);
	TEST(test_e2e_set_string);
	TEST(test_e2e_get_root);
	TEST(test_e2e_coerce_float_to_int);
	TEST(test_e2e_coerce_int_to_bool);
	TEST(test_e2e_coerce_int_to_enum);
	TEST(test_e2e_unsupported_coercion);
	TEST(test_e2e_sequential_ops);
	TEST(test_e2e_notify);
	TEST(test_e2e_notify_nested);

	printf("hash:\n");
	TEST(test_fnv1a32_vectors);
	TEST(test_fnv1a32_incremental);

	printf("manifest:\n");
	TEST(test_manifest_example);
	TEST(test_manifest_hash_covers_body);
	TEST(test_manifest_order);
	TEST(test_manifest_empty_group_invisible);
	TEST(test_manifest_no_leaves);
	TEST(test_manifest_float_bits);
	TEST(test_manifest_int_range);
	TEST(test_manifest_utf8);
	TEST(test_manifest_sink_error);
	TEST(test_manifest_rejects_invalid);

	printf("schema validation:\n");
	TEST(test_validate_ok);
	TEST(test_validate_no_root);
	TEST(test_validate_bad_name);
	TEST(test_validate_bad_root_name);
	TEST(test_validate_labels);
	TEST(test_validate_unit);
	TEST(test_validate_duplicate);
	TEST(test_validate_not_group);
	TEST(test_validate_bad_type);
	TEST(test_validate_too_deep);
	TEST(test_validate_path_too_long);
	TEST(test_schema_result_name);

	printf("server start:\n");
	TEST(test_server_start_rejects_invalid);
	TEST(test_server_schema_hash);

	printf("schema and manifest over the wire:\n");
	TEST(test_e2e_schema_get);
	TEST(test_e2e_schema_get_any);
	TEST(test_e2e_schema_get_other_root);
	TEST(test_e2e_manifest_get);
	TEST(test_e2e_manifest_get_other_root);
	TEST(test_e2e_manifest_chunked);

	printf("\n%d/%d tests passed\n", tests_run - tests_failed, tests_run);
	return tests_failed ? 1 : 0;
}
