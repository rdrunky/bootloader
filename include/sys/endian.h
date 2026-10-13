#ifndef SYS_ENDIAN_H
#define SYS_ENDIAN_H

#include <stdint.h>

static inline uint16_t get_le16(const void *data)
{
	const uint8_t *p = data;

	return (uint16_t)(p[0] | p[1] << 8);
}

static inline uint32_t get_le32(const void *data)
{
	const uint8_t *p = data;

	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint64_t get_le64(const void *data)
{
	const uint8_t *p = data;

	return (uint64_t)get_le32(p) | (uint64_t)get_le32(p + 4) << 32;
}

static inline uint16_t get_be16(const void *data)
{
	const uint8_t *p = data;

	return (uint16_t)(p[0] << 8 | p[1]);
}

static inline uint32_t get_be32(const void *data)
{
	const uint8_t *p = data;

	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static inline uint64_t get_be64(const void *data)
{
	const uint8_t *p = data;

	return (uint64_t)get_be32(p) << 32 | get_be32(p + 4);
}

static inline void put_le16(void *data, uint16_t value)
{
	uint8_t *p = data;

	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static inline void put_le32(void *data, uint32_t value)
{
	uint8_t *p = data;

	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

static inline void put_le64(void *data, uint64_t value)
{
	put_le32(data, (uint32_t)value);
	put_le32((uint8_t *)data + 4, (uint32_t)(value >> 32));
}

static inline void put_be16(void *data, uint16_t value)
{
	uint8_t *p = data;

	p[0] = (uint8_t)(value >> 8);
	p[1] = (uint8_t)value;
}

static inline void put_be32(void *data, uint32_t value)
{
	uint8_t *p = data;

	p[0] = (uint8_t)(value >> 24);
	p[1] = (uint8_t)(value >> 16);
	p[2] = (uint8_t)(value >> 8);
	p[3] = (uint8_t)value;
}

static inline void put_be64(void *data, uint64_t value)
{
	put_be32(data, (uint32_t)(value >> 32));
	put_be32((uint8_t *)data + 4, (uint32_t)value);
}

#endif
