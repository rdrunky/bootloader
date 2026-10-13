#ifndef DEVICETREE_PRIVATE_H
#define DEVICETREE_PRIVATE_H

#include <lib/devicetree.h>
#include <sys/endian.h>
#include <sys/macros.h>

#define TOKEN_BEGIN_NODE	1
#define TOKEN_END_NODE		2
#define TOKEN_PROP		3
#define TOKEN_NOP		4
#define TOKEN_END		9

#define TOKEN_SIZE		4
#define PROP_LENGTH		4
#define PROP_NAME		8
#define PROP_HEADER_SIZE	12
#define RESERVATION_SIZE	16
#define DEPTH_MAX		64

#define HEADER_SIZE		40
#define HEADER_MAGIC		0
#define HEADER_TOTAL_SIZE	4
#define HEADER_STRUCTS		8
#define HEADER_STRINGS		12
#define HEADER_RESERVATIONS	16
#define HEADER_VERSION		20
#define HEADER_LAST_COMPATIBLE	24
#define HEADER_BOOT_CPU		28
#define HEADER_STRINGS_SIZE	32
#define HEADER_STRUCTS_SIZE	36

#define VERSION_REQUIRED	17
#define VERSION_COMPATIBLE	16

static inline const uint8_t *dt_structs(const struct dt *dt)
{
	return dt->blob + dt->structs;
}

static inline uint32_t align_token(uint32_t value)
{
	return ALIGN_UP(value, TOKEN_SIZE);
}

int dt_token(const struct dt *dt, int offset, int *next);

#endif
