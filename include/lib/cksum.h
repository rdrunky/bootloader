#ifndef LIB_CKSUM_H
#define LIB_CKSUM_H

#include <stddef.h>
#include <stdint.h>

uint32_t crc32_update(uint32_t crc, const void *data, size_t length);
uint32_t crc32(const void *data, size_t length);

#endif
