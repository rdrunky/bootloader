#ifndef SYS_MEM_H
#define SYS_MEM_H

#include <lib/heap.h>
#include <lib/memmap.h>
#include <stdint.h>

void mem_init(void);
const struct memmap *mem_map(void);
struct heap *mem_heap(void);
int mem_claim(uint64_t base, uint64_t size, const char *owner);
int mem_allocate(uint64_t size, uint64_t align, const char *owner, uint64_t *base);
int mem_allocate_random(uint64_t size, uint64_t align, const char *owner, uint64_t random, uint64_t *base);
int mem_release(uint64_t base, uint64_t size, const char *owner);
void mem_release_owner(const char *owner);
int mem_map_device(uint64_t base, uint64_t size);

#endif
