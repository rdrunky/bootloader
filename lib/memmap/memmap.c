#include <errno.h>
#include <lib/memmap.h>
#include <string.h>
#include <sys/macros.h>

#define PAGE_MASK	(MEMMAP_PAGE_SIZE - 1)

static bool same_owner(const char *a, const char *b)
{
	if (a == NULL || b == NULL) {
		return a == b;
	}

	return a == b || strcmp(a, b) == 0;
}

static bool round_outward(uint64_t base, uint64_t size, uint64_t *start, uint64_t *end)
{
	if (size == 0 || base + size < base || base + size > UINT64_MAX - PAGE_MASK) {
		return false;
	}

	*start = ALIGN_DOWN(base, MEMMAP_PAGE_SIZE);
	*end = ALIGN_UP(base + size, MEMMAP_PAGE_SIZE);

	return true;
}

static bool round_inward(uint64_t base, uint64_t size, uint64_t *start, uint64_t *end)
{
	if (size == 0 || base + size < base || base > UINT64_MAX - PAGE_MASK) {
		return false;
	}

	*start = ALIGN_UP(base, MEMMAP_PAGE_SIZE);
	*end = ALIGN_DOWN(base + size, MEMMAP_PAGE_SIZE);

	return *start < *end;
}

static int insert(struct memmap *map, size_t index, uint64_t base, uint64_t end, const char *owner)
{
	if (map->count == MEMMAP_SEGMENTS_MAX) {
		return -ENOSPC;
	}

	memmove(&map->segments[index + 1], &map->segments[index], (map->count - index) * sizeof(map->segments[0]));
	map->segments[index] = (struct memmap_segment){ base, end, owner };
	map->count++;

	return 0;
}

static void merge(struct memmap *map)
{
	size_t i = 0;

	while (i + 1 < map->count) {
		struct memmap_segment *a = &map->segments[i];
		struct memmap_segment *b = a + 1;

		if (a->end == b->base && same_owner(a->owner, b->owner)) {
			a->end = b->end;
			memmove(b, b + 1, (map->count - i - 2) * sizeof(*b));
			map->count--;
		} else {
			i++;
		}
	}
}

static int split(struct memmap *map, uint64_t address)
{
	size_t i;

	for (i = 0; i < map->count; i++) {
		struct memmap_segment *segment = &map->segments[i];

		if (address > segment->base && address < segment->end) {
			uint64_t end = segment->end;

			segment->end = address;
			return insert(map, i + 1, address, end, segment->owner);
		}
	}

	return 0;
}

static int reassign(struct memmap *map, uint64_t base, uint64_t end, bool any, const char *from, const char *to)
{
	size_t i;
	int err = split(map, base);

	if (err == 0) {
		err = split(map, end);
	}

	if (err == 0) {
		for (i = 0; i < map->count; i++) {
			struct memmap_segment *segment = &map->segments[i];

			if (segment->base >= base && segment->end <= end && (any || same_owner(segment->owner, from))) {
				segment->owner = to;
			}
		}
	}

	merge(map);

	return err;
}

static int check_range(const struct memmap *map, uint64_t base, uint64_t end)
{
	uint64_t cursor = base;
	size_t i;

	for (i = 0; i < map->count && cursor < end; i++) {
		const struct memmap_segment *segment = &map->segments[i];

		if (segment->end <= cursor) {
			continue;
		}

		if (segment->base > cursor) {
			return -ERANGE;
		}

		if (segment->owner != NULL) {
			return -EBUSY;
		}

		cursor = segment->end;
	}

	return cursor >= end ? 0 : -ERANGE;
}

static bool valid_alignment(uint64_t *align)
{
	if (*align < MEMMAP_PAGE_SIZE) {
		*align = MEMMAP_PAGE_SIZE;
	}

	return (*align & (*align - 1)) == 0;
}

void memmap_init(struct memmap *map)
{
	map->count = 0;
}

int memmap_add(struct memmap *map, uint64_t base, uint64_t size)
{
	uint64_t end;
	size_t i = 0;
	int err = 0;

	if (!round_inward(base, size, &base, &end)) {
		return -EINVAL;
	}

	while (base < end && err == 0) {
		while (i < map->count && map->segments[i].end <= base) {
			i++;
		}

		if (i == map->count || map->segments[i].base >= end) {
			err = insert(map, i, base, end, NULL);
			break;
		}

		if (map->segments[i].base > base) {
			err = insert(map, i, base, map->segments[i].base, NULL);
			if (err != 0) {
				break;
			}
			i++;
		}

		base = map->segments[i].end;
	}

	merge(map);

	return err;
}

int memmap_reserve(struct memmap *map, uint64_t base, uint64_t size, const char *owner)
{
	uint64_t end;

	if (owner == NULL || !round_outward(base, size, &base, &end)) {
		return -EINVAL;
	}

	return reassign(map, base, end, false, NULL, owner);
}

int memmap_claim(struct memmap *map, uint64_t base, uint64_t size, const char *owner)
{
	uint64_t end;
	int err;

	if (owner == NULL || !round_outward(base, size, &base, &end)) {
		return -EINVAL;
	}

	err = check_range(map, base, end);
	if (err != 0) {
		return err;
	}

	return reassign(map, base, end, false, NULL, owner);
}

int memmap_alloc(struct memmap *map, uint64_t size, uint64_t align, const char *owner, uint64_t *base)
{
	uint64_t start;
	uint64_t end;
	size_t i;

	if (owner == NULL || !valid_alignment(&align) || !round_outward(0, size, &start, &end)) {
		return -EINVAL;
	}

	size = end;

	for (i = 0; i < map->count; i++) {
		const struct memmap_segment *segment = &map->segments[i];

		if (segment->owner != NULL || segment->base > UINT64_MAX - (align - 1)) {
			continue;
		}

		start = ALIGN_UP(segment->base, align);
		if (start < segment->end && segment->end - start >= size) {
			*base = start;
			return reassign(map, start, start + size, false, NULL, owner);
		}
	}

	return -ENOMEM;
}

int memmap_alloc_top(struct memmap *map, uint64_t size, uint64_t align, const char *owner, uint64_t *base)
{
	uint64_t start;
	uint64_t end;
	size_t i;

	if (owner == NULL || !valid_alignment(&align) || !round_outward(0, size, &start, &end)) {
		return -EINVAL;
	}

	size = end;

	for (i = map->count; i > 0; i--) {
		const struct memmap_segment *segment = &map->segments[i - 1];

		if (segment->owner != NULL || segment->end - segment->base < size) {
			continue;
		}

		start = ALIGN_DOWN(segment->end - size, align);
		if (start >= segment->base) {
			*base = start;
			return reassign(map, start, start + size, false, NULL, owner);
		}
	}

	return -ENOMEM;
}

static uint64_t free_slots(const struct memmap_segment *segment, uint64_t size, uint64_t align, uint64_t *first)
{
	uint64_t start;
	uint64_t last;

	if (segment->owner != NULL || segment->base > UINT64_MAX - (align - 1)) {
		return 0;
	}

	start = ALIGN_UP(segment->base, align);
	if (start >= segment->end || segment->end - start < size) {
		return 0;
	}

	last = ALIGN_DOWN(segment->end - size, align);
	*first = start;

	return (last - start) / align + 1;
}

int memmap_alloc_random(struct memmap *map, uint64_t size, uint64_t align, const char *owner, uint64_t random,
			uint64_t *base)
{
	uint64_t total = 0;
	uint64_t first = 0;
	uint64_t pick;
	uint64_t start;
	uint64_t end;
	size_t i;

	if (owner == NULL || !valid_alignment(&align) || !round_outward(0, size, &start, &end)) {
		return -EINVAL;
	}

	size = end;

	for (i = 0; i < map->count; i++) {
		total += free_slots(&map->segments[i], size, align, &first);
	}

	if (total == 0) {
		return -ENOMEM;
	}

	pick = random % total;
	for (i = 0; i < map->count; i++) {
		uint64_t slots = free_slots(&map->segments[i], size, align, &first);

		if (pick < slots) {
			*base = first + pick * align;
			return reassign(map, *base, *base + size, false, NULL, owner);
		}

		pick -= slots;
	}

	return -ENOMEM;
}

int memmap_release(struct memmap *map, uint64_t base, uint64_t size)
{
	uint64_t end;

	if (!round_outward(base, size, &base, &end)) {
		return -EINVAL;
	}

	return reassign(map, base, end, true, NULL, NULL);
}

int memmap_release_owned(struct memmap *map, uint64_t base, uint64_t size, const char *owner)
{
	uint64_t end;

	if (owner == NULL || !round_outward(base, size, &base, &end)) {
		return -EINVAL;
	}

	return reassign(map, base, end, false, owner, NULL);
}

const struct memmap_segment *memmap_find(const struct memmap *map, uint64_t address)
{
	size_t i;

	for (i = 0; i < map->count; i++) {
		if (address >= map->segments[i].base && address < map->segments[i].end) {
			return &map->segments[i];
		}
	}

	return NULL;
}

bool memmap_contains(const struct memmap *map, uint64_t base, uint64_t size)
{
	uint64_t cursor = base;
	size_t i;

	if (size == 0 || base + size < base) {
		return false;
	}

	for (i = 0; i < map->count && cursor < base + size; i++) {
		if (map->segments[i].end <= cursor) {
			continue;
		}

		if (map->segments[i].base > cursor) {
			return false;
		}

		cursor = map->segments[i].end;
	}

	return cursor >= base + size;
}

uint64_t memmap_total(const struct memmap *map)
{
	uint64_t total = 0;
	size_t i;

	for (i = 0; i < map->count; i++) {
		total += map->segments[i].end - map->segments[i].base;
	}

	return total;
}

uint64_t memmap_available(const struct memmap *map)
{
	uint64_t total = 0;
	size_t i;

	for (i = 0; i < map->count; i++) {
		if (map->segments[i].owner == NULL) {
			total += map->segments[i].end - map->segments[i].base;
		}
	}

	return total;
}
