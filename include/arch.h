#ifndef ARCH_H
#define ARCH_H

#include <compiler.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/bits.h>

#define ARCH_MAP_DEVICE		BIT(0)
#define ARCH_MAP_READONLY	BIT(1)
#define ARCH_MAP_EXECUTE	BIT(2)

extern uint64_t boot_el;
extern uint64_t boot_arg;
extern uint64_t handoff_el;

uintptr_t arch_image_start(void);
size_t arch_image_size(void);

int arch_random(uint64_t *value);

void arch_idle(void);
void arch_irq_disable(void);

uint64_t arch_timer_frequency(void);
uint64_t arch_timer_count(void);

void arch_dcache_clean_range(uintptr_t start, size_t size);
void arch_dcache_clean_invalidate_range(uintptr_t start, size_t size);
void arch_dcache_invalidate_range(uintptr_t start, size_t size);
void arch_dcache_clean_invalidate_all(void);
void arch_memory_barrier(void);

int arch_mmu_map(uint64_t base, uint64_t size, unsigned int flags);
int arch_mmu_map_image(void);
int arch_mmu_enable(void);

int arch_probe_read(uintptr_t address, unsigned int width, uint64_t *value);
int arch_probe_write(uintptr_t address, unsigned int width, uint64_t value);

void arch_backtrace(uintptr_t fp);

void arch_handoff(uintptr_t entry, uintptr_t argument) __noreturn;

#endif
