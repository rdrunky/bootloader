#ifndef LIB_MEMMAP_H
#define LIB_MEMMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEMMAP_SEGMENTS_MAX	128
#define MEMMAP_PAGE_SIZE	0x1000ULL

struct memmap_segment {
	uint64_t	base;
	uint64_t	end;
	const char	*owner;
};

struct memmap {
	struct memmap_segment	segments[MEMMAP_SEGMENTS_MAX];
	size_t			count;
};

void memmap_init(struct memmap *map);

int memmap_add(struct memmap *map, uint64_t base, uint64_t size);
int memmap_reserve(struct memmap *map, uint64_t base, uint64_t size, const char *owner);
int memmap_claim(struct memmap *map, uint64_t base, uint64_t size, const char *owner);
int memmap_alloc(struct memmap *map, uint64_t size, uint64_t align, const char *owner, uint64_t *base);
int memmap_alloc_top(struct memmap *map, uint64_t size, uint64_t align, const char *owner, uint64_t *base);
int memmap_alloc_random(struct memmap *map, uint64_t size, uint64_t align, const char *owner, uint64_t random,
			uint64_t *base);
int memmap_release(struct memmap *map, uint64_t base, uint64_t size);
int memmap_release_owned(struct memmap *map, uint64_t base, uint64_t size, const char *owner);

const struct memmap_segment *memmap_find(const struct memmap *map, uint64_t address);
bool memmap_contains(const struct memmap *map, uint64_t base, uint64_t size);
uint64_t memmap_total(const struct memmap *map);
uint64_t memmap_available(const struct memmap *map);

#endif
