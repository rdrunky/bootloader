#include <errno.h>
#include <lib/heap.h>
#include <stdint.h>
#include <string.h>

#include "harness.h"

#define ARENA_SIZE	(256 * 1024)
#define LIVE_MAX	64
#define MODEL_ROUNDS	100000

static uint8_t arena[ARENA_SIZE] __attribute__((aligned(4096)));
static struct heap heap;

struct live {
	uint8_t	*data;
	size_t	size;
	uint8_t	pattern;
};

static void setup(void)
{
	heap_init(&heap, arena, sizeof(arena));
}

static size_t free_blocks(void)
{
	struct heap_stats stats;

	heap_stats(&heap, &stats);

	return stats.free_blocks;
}

TEST(heap, starts_as_one_free_block)
{
	struct heap_stats stats;

	setup();
	heap_stats(&heap, &stats);
	CHECK_EQ(stats.size, ARENA_SIZE);
	CHECK_EQ(stats.used, 0);
	CHECK_EQ(stats.free_blocks, 1);
	CHECK_EQ(heap_check(&heap), 0);
	CHECK_EQ(heap_init(&heap, NULL, 4096), -EINVAL);
	CHECK_EQ(heap_init(&heap, arena, 32), -EINVAL);
}

TEST(heap, allocates_aligned_and_reuses)
{
	uint8_t *a;
	uint8_t *b;
	uint8_t *c;

	setup();
	a = heap_alloc(&heap, 100, 0);
	b = heap_alloc(&heap, 1, 4096);
	c = heap_alloc(&heap, 0, 64);
	CHECK(a != NULL && b != NULL && c != NULL);
	CHECK_EQ((uintptr_t)a % 16, 0);
	CHECK_EQ((uintptr_t)b % 4096, 0);
	CHECK_EQ((uintptr_t)c % 64, 0);
	CHECK_EQ(heap_check(&heap), 0);
	CHECK_EQ(heap_free(&heap, b), 0);
	CHECK(heap_alloc(&heap, 1, 4096) == b);
	CHECK_EQ(heap_check(&heap), 0);
}

TEST(heap, refuses_impossible_requests)
{
	setup();
	CHECK(heap_alloc(&heap, ARENA_SIZE, 0) == NULL);
	CHECK(heap_alloc(&heap, SIZE_MAX, 0) == NULL);
	CHECK(heap_alloc(&heap, SIZE_MAX - 20, 0) == NULL);
	CHECK(heap_alloc(&heap, 16, 48) == NULL);
	CHECK(heap_alloc(&heap, 16, SIZE_MAX / 2 + 1) == NULL);
	CHECK_EQ(heap_check(&heap), 0);
}

TEST(heap, coalesces_neighbours)
{
	void *a;
	void *b;
	void *c;
	void *d;

	setup();
	a = heap_alloc(&heap, 1000, 0);
	b = heap_alloc(&heap, 1000, 0);
	c = heap_alloc(&heap, 1000, 0);
	d = heap_alloc(&heap, 1000, 0);
	CHECK_EQ(heap_free(&heap, b), 0);
	CHECK_EQ(heap_free(&heap, d), 0);
	CHECK_EQ(free_blocks(), 2);
	CHECK_EQ(heap_free(&heap, c), 0);
	CHECK_EQ(free_blocks(), 1);
	CHECK_EQ(heap_free(&heap, a), 0);
	CHECK_EQ(free_blocks(), 1);
	CHECK_EQ(heap_check(&heap), 0);
}

TEST(heap, detects_misuse)
{
	uint8_t *a;
	uint8_t *b;

	setup();
	a = heap_alloc(&heap, 40, 0);
	b = heap_alloc(&heap, 40, 0);
	CHECK_EQ(heap_free(&heap, NULL), 0);
	CHECK_EQ(heap_free(&heap, a + 16), -EUCLEAN);
	CHECK_EQ(heap_free(&heap, a + 1), -EFAULT);
	CHECK_EQ(heap_free(&heap, arena + ARENA_SIZE), -EFAULT);
	CHECK_EQ(heap_free(&heap, a), 0);
	CHECK_EQ(heap_free(&heap, a), -EINVAL);

	b[40] = 0;
	CHECK_EQ(heap_check(&heap), -EOVERFLOW);
	CHECK_EQ(heap_free(&heap, b), -EOVERFLOW);

	setup();
	b = heap_alloc(&heap, 40, 0);
	a = heap_alloc(&heap, 40, 0);
	memset(b, 0x41, (size_t)(a - b) + 16);
	CHECK_EQ(heap_free(&heap, a), -EUCLEAN);
	CHECK(heap_check(&heap) != 0);
}

TEST(heap, poisons_freed_memory)
{
	uint8_t *a;
	uint8_t *b;

	setup();
	a = heap_alloc(&heap, 64, 0);
	b = heap_alloc(&heap, 64, 0);
	memset(a, 0x11, 64);
	CHECK_EQ(heap_free(&heap, a), 0);
	CHECK_EQ(a[0], 0xdb);
	CHECK_EQ(a[63], 0xdb);
	CHECK_EQ(heap_free(&heap, b), 0);
}

TEST(heap, resizes_keeping_contents)
{
	uint8_t *a;
	uint8_t *b;
	unsigned int i;

	setup();
	a = heap_alloc(&heap, 100, 0);
	for (i = 0; i < 100; i++) {
		a[i] = (uint8_t)i;
	}

	b = a;
	CHECK_EQ(heap_resize(&heap, (void **)&b, 30), 0);
	CHECK(b == a);
	CHECK_EQ(heap_resize(&heap, (void **)&b, 5000), 0);
	CHECK(b != a);
	for (i = 0; i < 30; i++) {
		CHECK_EQ(b[i], i);
	}

	a = NULL;
	CHECK_EQ(heap_resize(&heap, (void **)&a, 10), 0);
	CHECK(a != NULL);
	CHECK_EQ(heap_resize(&heap, (void **)&b, SIZE_MAX), -ENOMEM);
	CHECK_EQ(heap_resize(&heap, (void **)&b, ARENA_SIZE), -ENOMEM);
	CHECK_EQ(heap_free(&heap, a), 0);
	CHECK_EQ(heap_resize(&heap, (void **)&a, 10), -EINVAL);
	CHECK_EQ(heap_check(&heap), 0);
}

static uint64_t random_state = 0xd1b54a32d192ed03ULL;

static bool intact(const struct live *item)
{
	size_t i;

	for (i = 0; i < item->size; i++) {
		if (item->data[i] != item->pattern) {
			return false;
		}
	}

	return true;
}

static void fill(struct live *item)
{
	item->pattern = (uint8_t)test_random(&random_state);
	memset(item->data, item->pattern, item->size);
}

static size_t random_size(void)
{
	switch (test_random(&random_state) % 4) {
	case 0:
		return test_random(&random_state) % 16;
	case 1:
		return test_random(&random_state) % 256;
	case 2:
		return test_random(&random_state) % 4096;
	default:
		return test_random(&random_state) % 20000;
	}
}

static bool step(struct live *items)
{
	struct live *item = &items[test_random(&random_state) % LIVE_MAX];
	size_t size = random_size();
	uint8_t *data;

	if (item->data != NULL && !intact(item)) {
		return false;
	}

	switch (test_random(&random_state) % 3) {
	case 0:
		if (item->data != NULL) {
			if (heap_free(&heap, item->data) != 0) {
				return false;
			}
			item->data = NULL;
		}
		break;
	case 1:
		if (item->data == NULL) {
			data = heap_alloc(&heap, size, (size_t)1 << (test_random(&random_state) % 10));
			if (data != NULL) {
				item->data = data;
				item->size = size;
				fill(item);
			}
		}
		break;
	default:
		if (item->data != NULL) {
			void *resized = item->data;
			int err = heap_resize(&heap, &resized, size);

			if (err == 0) {
				item->data = resized;
				item->size = size < item->size ? size : item->size;
				if (!intact(item)) {
					return false;
				}
				item->size = size;
				fill(item);
			} else if (err != -ENOMEM) {
				return false;
			}
		}
		break;
	}

	return heap_check(&heap) == 0;
}

TEST(heap, survives_random_workload)
{
	struct live items[LIVE_MAX];
	struct heap_stats stats;
	unsigned int round;
	unsigned int i;

	setup();
	memset(items, 0, sizeof(items));

	for (round = 0; round < MODEL_ROUNDS; round++) {
		CHECK(step(items));
	}

	for (i = 0; i < LIVE_MAX; i++) {
		if (items[i].data != NULL) {
			CHECK(intact(&items[i]));
			CHECK_EQ(heap_free(&heap, items[i].data), 0);
		}
	}

	heap_stats(&heap, &stats);
	CHECK_EQ(stats.used, 0);
	CHECK_EQ(stats.allocations, 0);
	CHECK_EQ(stats.free_blocks, 1);
	CHECK_EQ(heap_check(&heap), 0);
}
