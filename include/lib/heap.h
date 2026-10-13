#ifndef LIB_HEAP_H
#define LIB_HEAP_H

#include <stddef.h>
#include <stdint.h>

struct heap_block;

struct heap {
	uint8_t			*start;
	uint8_t			*end;
	struct heap_block	*free;
	size_t			used;
	size_t			peak;
	size_t			allocations;
};

struct heap_stats {
	size_t	size;
	size_t	used;
	size_t	peak;
	size_t	allocations;
	size_t	free_blocks;
	size_t	largest_free;
};

int heap_init(struct heap *heap, void *base, size_t size);
void *heap_alloc(struct heap *heap, size_t size, size_t align);
int heap_resize(struct heap *heap, void **pointer, size_t size);
int heap_free(struct heap *heap, void *pointer);
int heap_check(const struct heap *heap);
void heap_stats(const struct heap *heap, struct heap_stats *stats);

#endif
