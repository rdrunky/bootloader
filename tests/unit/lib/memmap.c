#include <errno.h>
#include <lib/memmap.h>
#include <stdint.h>
#include <string.h>
#include <sys/macros.h>

#include "harness.h"

#define PAGE		MEMMAP_PAGE_SIZE
#define MODEL_PAGES	64
#define MODEL_ROUNDS	200000

static struct memmap map;

static const char *const owners[] = { "boot", "heap", "kernel", "reserved" };

static void setup(void)
{
	memmap_init(&map);
}

static bool well_formed(const struct memmap *m)
{
	size_t i;

	for (i = 0; i < m->count; i++) {
		const struct memmap_segment *segment = &m->segments[i];

		if (segment->base >= segment->end || segment->base % PAGE != 0 || segment->end % PAGE != 0) {
			return false;
		}

		if (i > 0) {
			const struct memmap_segment *previous = &m->segments[i - 1];
			bool same = previous->owner == segment->owner ||
				    (previous->owner != NULL && segment->owner != NULL &&
				     strcmp(previous->owner, segment->owner) == 0);

			if (previous->end > segment->base || (previous->end == segment->base && same)) {
				return false;
			}
		}
	}

	return true;
}

TEST(memmap, adds_ram_page_aligned_inward)
{
	setup();
	CHECK_EQ(memmap_add(&map, 0x40000123, 0x10000), 0);
	CHECK_EQ(map.count, 1);
	CHECK_EQ(map.segments[0].base, 0x40001000);
	CHECK_EQ(map.segments[0].end, 0x40010000);
	CHECK_EQ(memmap_add(&map, 0x1000, 0x800), -EINVAL);
	CHECK_EQ(memmap_add(&map, UINT64_MAX - 0x1000, 0x2000), -EINVAL);
}

TEST(memmap, merges_overlapping_ram)
{
	setup();
	CHECK_EQ(memmap_add(&map, 0x40000000, 0x100000), 0);
	CHECK_EQ(memmap_add(&map, 0x40080000, 0x100000), 0);
	CHECK_EQ(memmap_add(&map, 0x80000000, 0x100000), 0);
	CHECK_EQ(map.count, 2);
	CHECK_EQ(memmap_total(&map), 0x280000);
	CHECK(well_formed(&map));
}

TEST(memmap, reserves_without_stealing)
{
	setup();
	CHECK_EQ(memmap_add(&map, 0x40000000, 0x100000), 0);
	CHECK_EQ(memmap_reserve(&map, 0x40010000, 0x1000, "boot"), 0);
	CHECK_EQ(memmap_reserve(&map, 0x4000f800, 0x2000, "reserved"), 0);
	CHECK_STR(memmap_find(&map, 0x40010000)->owner, "boot");
	CHECK_STR(memmap_find(&map, 0x4000f000)->owner, "reserved");
	CHECK_STR(memmap_find(&map, 0x40011000)->owner, "reserved");
	CHECK(memmap_find(&map, 0x40012000)->owner == NULL);
	CHECK_EQ(memmap_reserve(&map, 0x90000000, 0x1000, "reserved"), 0);
	CHECK(memmap_find(&map, 0x90000000) == NULL);
	CHECK(well_formed(&map));
}

TEST(memmap, claims_only_free_ram)
{
	setup();
	CHECK_EQ(memmap_add(&map, 0x40000000, 0x100000), 0);
	CHECK_EQ(memmap_claim(&map, 0x40020000, 0x20000, "kernel"), 0);
	CHECK_EQ(memmap_claim(&map, 0x4003f000, 0x2000, "heap"), -EBUSY);
	CHECK_EQ(memmap_claim(&map, 0x400ff000, 0x2000, "heap"), -ERANGE);
	CHECK_EQ(memmap_claim(&map, 0x3ffff000, 0x2000, "heap"), -ERANGE);
	CHECK_EQ(memmap_claim(&map, 0x40040000, 0x1000, NULL), -EINVAL);
	CHECK_EQ(memmap_available(&map), 0xe0000);
}

TEST(memmap, allocates_low_and_high)
{
	uint64_t base;

	setup();
	CHECK_EQ(memmap_add(&map, 0x40000000, 0x1000000), 0);
	CHECK_EQ(memmap_reserve(&map, 0x40000000, 0x81000, "boot"), 0);
	CHECK_EQ(memmap_alloc(&map, 0x300000, 0x200000, "kernel", &base), 0);
	CHECK_EQ(base, 0x40200000);
	CHECK_EQ(memmap_alloc_top(&map, 0x123, 0, "heap", &base), 0);
	CHECK_EQ(base, 0x40fff000);
	CHECK_EQ(memmap_alloc(&map, 0x1000, 0x3000, "heap", &base), -EINVAL);
	CHECK_EQ(memmap_alloc(&map, 0x2000000, 0, "heap", &base), -ENOMEM);
	CHECK_EQ(memmap_alloc(&map, 0, 0, "heap", &base), -EINVAL);
	CHECK(well_formed(&map));
}

TEST(memmap, allocates_every_random_slot_once)
{
	static const uint64_t slots[] = { 0x40200000, 0x40400000, 0x40a00000, 0x40c00000 };
	struct memmap saved;
	uint64_t random;
	uint64_t base;

	setup();
	CHECK_EQ(memmap_add(&map, 0x40000000, 0x1000000), 0);
	CHECK_EQ(memmap_reserve(&map, 0x40000000, 0x81000, "boot"), 0);
	CHECK_EQ(memmap_claim(&map, 0x40800000, 0x200000, "heap"), 0);
	saved = map;

	for (random = 0; random < 2 * ARRAY_SIZE(slots); random++) {
		map = saved;
		CHECK_EQ(memmap_alloc_random(&map, 0x300000, 0x200000, "kernel", random, &base), 0);
		CHECK_EQ(base, slots[random % ARRAY_SIZE(slots)]);
		CHECK_STR(memmap_find(&map, base)->owner, "kernel");
		CHECK_STR(memmap_find(&map, base + 0x2ff000)->owner, "kernel");
		CHECK(well_formed(&map));
	}

	map = saved;
	CHECK_EQ(memmap_alloc_random(&map, 0x300000, 0x200000, "kernel", UINT64_MAX, &base), 0);
	CHECK_EQ(base, slots[UINT64_MAX % ARRAY_SIZE(slots)]);
	CHECK_EQ(memmap_alloc_random(&map, 0x700000, 0x200000, "kernel", 0, &base), -ENOMEM);
	CHECK_EQ(memmap_alloc_random(&map, 0x1000, 0x3000, "kernel", 0, &base), -EINVAL);
	CHECK_EQ(memmap_alloc_random(&map, 0, 0, "kernel", 0, &base), -EINVAL);
	CHECK_EQ(memmap_alloc_random(&map, 0x1000, 0, NULL, 0, &base), -EINVAL);
}

TEST(memmap, releases_and_merges)
{
	uint64_t base;

	setup();
	CHECK_EQ(memmap_add(&map, 0x40000000, 0x100000), 0);
	CHECK_EQ(memmap_alloc(&map, 0x10000, 0, "heap", &base), 0);
	CHECK_EQ(memmap_release(&map, base, 0x10000), 0);
	CHECK_EQ(map.count, 1);
	CHECK_EQ(memmap_available(&map), 0x100000);
}

TEST(memmap, releases_only_one_owner)
{
	setup();
	CHECK_EQ(memmap_add(&map, 0x40000000, 0x100000), 0);
	CHECK_EQ(memmap_claim(&map, 0x40010000, 0x10000, "kernel"), 0);
	CHECK_EQ(memmap_claim(&map, 0x40020000, 0x10000, "heap"), 0);
	CHECK_EQ(memmap_release_owned(&map, 0x40000000, 0x100000, "kernel"), 0);
	CHECK(memmap_find(&map, 0x40010000)->owner == NULL);
	CHECK_STR(memmap_find(&map, 0x40020000)->owner, "heap");
	CHECK_EQ(memmap_release_owned(&map, 0x40000000, 0x1000, NULL), -EINVAL);
	CHECK(well_formed(&map));
}

TEST(memmap, checks_containment)
{
	setup();
	CHECK_EQ(memmap_add(&map, 0x40000000, 0x100000), 0);
	CHECK_EQ(memmap_add(&map, 0x40100000, 0x100000), 0);
	CHECK_EQ(memmap_add(&map, 0x40300000, 0x100000), 0);
	CHECK(memmap_contains(&map, 0x400ff000, 0x2000));
	CHECK(!memmap_contains(&map, 0x401ff000, 0x2000));
	CHECK(!memmap_contains(&map, 0x40000000, 0));
	CHECK(!memmap_contains(&map, UINT64_MAX, 2));
}

TEST(memmap, reports_full_table)
{
	uint64_t base;
	unsigned int i;
	int err = 0;

	setup();
	CHECK_EQ(memmap_add(&map, 0, MEMMAP_SEGMENTS_MAX * 2 * PAGE), 0);
	for (i = 0; i < MEMMAP_SEGMENTS_MAX && err == 0; i++) {
		err = memmap_claim(&map, (uint64_t)i * 2 * PAGE, PAGE, i % 2 ? "heap" : "boot");
	}

	CHECK_EQ(err, -ENOSPC);
	CHECK(well_formed(&map));
	err = memmap_alloc(&map, PAGE, 0, "kernel", &base);
	CHECK(err == 0 || err == -ENOSPC);
	CHECK(well_formed(&map));
}

static uint64_t random_state = 0x9e3779b97f4a7c15ULL;

struct model {
	bool		ram[MODEL_PAGES];
	const char	*owner[MODEL_PAGES];
};

static bool model_matches(const struct model *model)
{
	unsigned int page;

	for (page = 0; page < MODEL_PAGES; page++) {
		const struct memmap_segment *segment = memmap_find(&map, page * PAGE);

		if (model->ram[page] != (segment != NULL)) {
			return false;
		}

		if (segment != NULL && segment->owner != model->owner[page]) {
			return false;
		}
	}

	return true;
}

static void model_assign(struct model *model, unsigned int first, unsigned int count, const char *owner, bool only_free)
{
	unsigned int page;

	for (page = first; page < first + count && page < MODEL_PAGES; page++) {
		if (model->ram[page] && (!only_free || model->owner[page] == NULL)) {
			model->owner[page] = owner;
		}
	}
}

static bool model_free(const struct model *model, unsigned int first, unsigned int count)
{
	unsigned int page;

	for (page = first; page < first + count; page++) {
		if (page >= MODEL_PAGES || !model->ram[page] || model->owner[page] != NULL) {
			return false;
		}
	}

	return true;
}

static int allocate(uint64_t size, const char *owner, uint64_t *base)
{
	switch (test_random(&random_state) % 3) {
	case 0:
		return memmap_alloc(&map, size, 0, owner, base);
	case 1:
		return memmap_alloc_top(&map, size, 0, owner, base);
	default:
		return memmap_alloc_random(&map, size, 0, owner, test_random(&random_state), base);
	}
}

static bool model_step(struct model *model)
{
	unsigned int first = test_random(&random_state) % MODEL_PAGES;
	unsigned int count = 1 + test_random(&random_state) % 8;
	const char *owner = owners[test_random(&random_state) % 4];
	uint64_t base;
	unsigned int page;
	int err;

	if (first + count > MODEL_PAGES) {
		count = MODEL_PAGES - first;
	}

	switch (test_random(&random_state) % 6) {
	case 0:
		if (memmap_add(&map, first * PAGE, count * PAGE) == 0) {
			for (page = first; page < first + count && page < MODEL_PAGES; page++) {
				model->ram[page] = true;
			}
		}
		break;
	case 1:
		if (memmap_reserve(&map, first * PAGE, count * PAGE, owner) == 0) {
			model_assign(model, first, count, owner, true);
		}
		break;
	case 2:
		err = memmap_claim(&map, first * PAGE, count * PAGE, owner);
		if (err == 0) {
			if (!model_free(model, first, count)) {
				return false;
			}
			model_assign(model, first, count, owner, true);
		} else if (err != -ENOSPC && model_free(model, first, count)) {
			return false;
		}
		break;
	case 3:
	case 4:
		err = allocate(count * PAGE, owner, &base);
		if (err == 0) {
			if (base % PAGE != 0 || !model_free(model, (unsigned int)(base / PAGE), count)) {
				return false;
			}
			model_assign(model, (unsigned int)(base / PAGE), count, owner, true);
		}
		break;
	default:
		if (memmap_release(&map, first * PAGE, count * PAGE) == 0) {
			model_assign(model, first, count, NULL, false);
		}
		break;
	}

	return true;
}

TEST(memmap, agrees_with_page_model)
{
	struct model model;
	unsigned int round;

	setup();
	memset(&model, 0, sizeof(model));

	for (round = 0; round < MODEL_ROUNDS; round++) {
		CHECK(model_step(&model));
		CHECK(well_formed(&map));
		CHECK(model_matches(&model));
	}
}
