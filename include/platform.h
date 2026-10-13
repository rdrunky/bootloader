#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

void platform_init(void);
uintptr_t platform_devicetree(void);
void platform_memory(uint64_t *base, uint64_t *size);

#endif
