#include <errno.h>
#include <lib/heap.h>
#include <stdbool.h>
#include <string.h>
#include <sys/macros.h>

#define BLOCK_USED	0x55534544u
#define BLOCK_FREE	0x46524545u
#define SEAL_KEY	0x8f3a6c5bu
#define GRANULE		16
#define CANARY		0x5a
#define CANARY_MIN	8
#define POISON		0xdb

struct heap_block {
	uint32_t		magic;
	uint32_t		seal;
	size_t			size;
	size_t			prev_size;
	size_t			requested;
	struct heap_block	*next_free;
	struct heap_block	*prev_free;
};

#define HEADER_SIZE	sizeof(struct heap_block)
#define BLOCK_MIN	(HEADER_SIZE + GRANULE)

_Static_assert(HEADER_SIZE % GRANULE == 0, "block header keeps payloads aligned");

static uint32_t seal_of(const struct heap_block *block)
{
	uint64_t mix = (uint64_t)block->size ^ (uint64_t)block->prev_size << 7 ^ (uint64_t)block->requested << 13;

	return block->magic ^ SEAL_KEY ^ (uint32_t)mix ^ (uint32_t)(mix >> 32);
}

static void seal(struct heap_block *block, uint32_t magic)
{
	block->magic = magic;
	block->seal = seal_of(block);
}

static bool intact(const struct heap_block *block, uint32_t magic)
{
	return block->magic == magic && block->seal == seal_of(block);
}

static uint8_t *payload(struct heap_block *block)
{
	return (uint8_t *)block + HEADER_SIZE;
}

static struct heap_block *block_at(const uint8_t *address)
{
	return (struct heap_block *)(uintptr_t)address;
}

static struct heap_block *next_block(const struct heap *heap, struct heap_block *block)
{
	uint8_t *next = (uint8_t *)block + block->size;

	return next < heap->end ? block_at(next) : NULL;
}

static struct heap_block *prev_block(const struct heap *heap, struct heap_block *block)
{
	if (block->prev_size == 0 || (size_t)((uint8_t *)block - heap->start) < block->prev_size) {
		return NULL;
	}

	return block_at((uint8_t *)block - block->prev_size);
}

static bool block_need(size_t size, size_t *need)
{
	if (size > SIZE_MAX - HEADER_SIZE - CANARY_MIN - GRANULE) {
		return false;
	}

	*need = ALIGN_UP(HEADER_SIZE + size + CANARY_MIN, GRANULE);

	return true;
}

static void unlink_free(struct heap *heap, struct heap_block *block)
{
	if (block->prev_free != NULL) {
		block->prev_free->next_free = block->next_free;
	} else {
		heap->free = block->next_free;
	}

	if (block->next_free != NULL) {
		block->next_free->prev_free = block->prev_free;
	}

	block->next_free = NULL;
	block->prev_free = NULL;
}

static void link_free(struct heap *heap, struct heap_block *block)
{
	struct heap_block *before = NULL;
	struct heap_block *after = heap->free;

	while (after != NULL && after < block) {
		before = after;
		after = after->next_free;
	}

	block->prev_free = before;
	block->next_free = after;

	if (before != NULL) {
		before->next_free = block;
	} else {
		heap->free = block;
	}

	if (after != NULL) {
		after->prev_free = block;
	}
}

static void update_successor(struct heap *heap, struct heap_block *block)
{
	struct heap_block *next = next_block(heap, block);

	if (next != NULL) {
		next->prev_size = block->size;
		seal(next, next->magic);
	}
}

static struct heap_block *carve(struct heap *heap, struct heap_block *block, size_t size)
{
	struct heap_block *rest;

	if (block->size - size < BLOCK_MIN) {
		return NULL;
	}

	rest = block_at((uint8_t *)block + size);
	rest->size = block->size - size;
	rest->prev_size = size;
	rest->requested = 0;
	rest->next_free = NULL;
	rest->prev_free = NULL;
	seal(rest, BLOCK_FREE);

	block->size = size;
	seal(block, block->magic);
	update_successor(heap, rest);

	return rest;
}

static void join(struct heap *heap, struct heap_block *block, struct heap_block *next)
{
	block->size += next->size;
	seal(block, block->magic);
	update_successor(heap, block);
}

static void release_tail(struct heap *heap, struct heap_block *rest)
{
	struct heap_block *next = next_block(heap, rest);

	if (next != NULL && intact(next, BLOCK_FREE)) {
		unlink_free(heap, next);
		join(heap, rest, next);
	}

	link_free(heap, rest);
}

static void mark_used(struct heap *heap, struct heap_block *block, size_t requested)
{
	block->requested = requested;
	seal(block, BLOCK_USED);
	memset(payload(block) + requested, CANARY, block->size - HEADER_SIZE - requested);

	heap->used += block->size;
	heap->allocations++;
	if (heap->used > heap->peak) {
		heap->peak = heap->used;
	}
}

static bool canary_intact(const struct heap_block *block)
{
	const uint8_t *tail = (const uint8_t *)block + HEADER_SIZE + block->requested;
	size_t count = block->size - HEADER_SIZE - block->requested;
	size_t i;

	for (i = 0; i < count; i++) {
		if (tail[i] != CANARY) {
			return false;
		}
	}

	return true;
}

int heap_init(struct heap *heap, void *base, size_t size)
{
	uintptr_t start;
	uintptr_t end;
	struct heap_block *block;

	if (base == NULL || (uintptr_t)base > UINTPTR_MAX - size) {
		return -EINVAL;
	}

	start = ALIGN_UP((uintptr_t)base, GRANULE);
	end = ALIGN_DOWN((uintptr_t)base + size, GRANULE);
	if (end <= start || end - start < BLOCK_MIN) {
		return -EINVAL;
	}

	*heap = (struct heap){
		.start = (uint8_t *)start,
		.end = (uint8_t *)end,
	};

	block = block_at(heap->start);
	block->size = end - start;
	block->prev_size = 0;
	block->requested = 0;
	block->next_free = NULL;
	block->prev_free = NULL;
	seal(block, BLOCK_FREE);
	heap->free = block;

	return 0;
}

static size_t front_gap(struct heap_block *block, size_t align)
{
	uintptr_t first = (uintptr_t)payload(block);
	size_t gap = (size_t)(ALIGN_UP(first, align) - first);

	if (gap != 0 && gap < BLOCK_MIN) {
		gap += ALIGN_UP(BLOCK_MIN - gap, align);
	}

	return gap;
}

static struct heap_block *fit(struct heap *heap, struct heap_block *block, size_t need, size_t align)
{
	size_t gap = front_gap(block, align);
	struct heap_block *used = block;
	struct heap_block *rest;

	if (gap > block->size || block->size - gap < need) {
		return NULL;
	}

	unlink_free(heap, block);

	if (gap != 0) {
		used = carve(heap, block, gap);
		link_free(heap, block);
	}

	rest = carve(heap, used, need);
	if (rest != NULL) {
		link_free(heap, rest);
	}

	return used;
}

void *heap_alloc(struct heap *heap, size_t size, size_t align)
{
	struct heap_block *block;
	size_t need;

	if (align < GRANULE) {
		align = GRANULE;
	}

	if ((align & (align - 1)) != 0 || align > SIZE_MAX / 4 || !block_need(size, &need)) {
		return NULL;
	}

	for (block = heap->free; block != NULL; block = block->next_free) {
		struct heap_block *used;

		if (!intact(block, BLOCK_FREE)) {
			return NULL;
		}

		used = fit(heap, block, need, align);
		if (used != NULL) {
			mark_used(heap, used, size);
			return payload(used);
		}
	}

	return NULL;
}

static struct heap_block *lookup(const struct heap *heap, void *pointer, int *err)
{
	uint8_t *address = pointer;
	struct heap_block *block;

	if (address < heap->start + HEADER_SIZE || address >= heap->end || ((uintptr_t)address & (GRANULE - 1)) != 0) {
		*err = -EFAULT;
		return NULL;
	}

	block = block_at(address - HEADER_SIZE);
	if (intact(block, BLOCK_FREE)) {
		*err = -EINVAL;
		return NULL;
	}

	if (!intact(block, BLOCK_USED) || block->size > (size_t)(heap->end - (uint8_t *)block) ||
	    block->requested > block->size - HEADER_SIZE) {
		*err = -EUCLEAN;
		return NULL;
	}

	if (!canary_intact(block)) {
		*err = -EOVERFLOW;
		return NULL;
	}

	*err = 0;

	return block;
}

int heap_free(struct heap *heap, void *pointer)
{
	struct heap_block *block;
	struct heap_block *neighbour;
	int err;

	if (pointer == NULL) {
		return 0;
	}

	block = lookup(heap, pointer, &err);
	if (block == NULL) {
		return err;
	}

	heap->used -= block->size;
	heap->allocations--;

	memset(payload(block), POISON, block->size - HEADER_SIZE);
	block->requested = 0;
	block->next_free = NULL;
	block->prev_free = NULL;
	seal(block, BLOCK_FREE);

	neighbour = next_block(heap, block);
	if (neighbour != NULL && intact(neighbour, BLOCK_FREE)) {
		unlink_free(heap, neighbour);
		join(heap, block, neighbour);
	}

	neighbour = prev_block(heap, block);
	if (neighbour != NULL && intact(neighbour, BLOCK_FREE)) {
		unlink_free(heap, neighbour);
		join(heap, neighbour, block);
		block = neighbour;
	}

	link_free(heap, block);

	return 0;
}

int heap_resize(struct heap *heap, void **pointer, size_t size)
{
	struct heap_block *block;
	struct heap_block *rest;
	uint8_t *moved;
	size_t need;
	int err;

	if (*pointer == NULL) {
		*pointer = heap_alloc(heap, size, GRANULE);
		return *pointer != NULL ? 0 : -ENOMEM;
	}

	block = lookup(heap, *pointer, &err);
	if (block == NULL) {
		return err;
	}

	if (!block_need(size, &need)) {
		return -ENOMEM;
	}

	if (need <= block->size) {
		heap->used -= block->size;
		heap->allocations--;

		rest = carve(heap, block, need);
		if (rest != NULL) {
			release_tail(heap, rest);
		}

		mark_used(heap, block, size);
		return 0;
	}

	moved = heap_alloc(heap, size, GRANULE);
	if (moved == NULL) {
		return -ENOMEM;
	}

	memcpy(moved, *pointer, block->requested);
	err = heap_free(heap, *pointer);
	*pointer = moved;

	return err;
}

int heap_check(const struct heap *heap)
{
	const struct heap_block *free = heap->free;
	const uint8_t *cursor = heap->start;
	size_t previous = 0;
	bool previous_free = false;

	while (cursor < heap->end) {
		const struct heap_block *block = (const struct heap_block *)(uintptr_t)cursor;
		bool used = intact(block, BLOCK_USED);

		if (!used && !intact(block, BLOCK_FREE)) {
			return -EUCLEAN;
		}

		if (block->size < BLOCK_MIN || block->size % GRANULE != 0 ||
		    block->size > (size_t)(heap->end - cursor) || block->prev_size != previous) {
			return -EUCLEAN;
		}

		if (used && (block->requested > block->size - HEADER_SIZE || !canary_intact(block))) {
			return -EOVERFLOW;
		}

		if (!used) {
			if (previous_free || free != block) {
				return -EUCLEAN;
			}
			free = free->next_free;
		}

		previous_free = !used;
		previous = block->size;
		cursor += block->size;
	}

	return cursor == heap->end && free == NULL ? 0 : -EUCLEAN;
}

void heap_stats(const struct heap *heap, struct heap_stats *stats)
{
	const struct heap_block *block;

	*stats = (struct heap_stats){
		.size = (size_t)(heap->end - heap->start),
		.used = heap->used,
		.peak = heap->peak,
		.allocations = heap->allocations,
	};

	for (block = heap->free; block != NULL; block = block->next_free) {
		stats->free_blocks++;
		if (block->size - HEADER_SIZE > stats->largest_free) {
			stats->largest_free = block->size - HEADER_SIZE;
		}
	}
}
