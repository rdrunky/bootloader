#ifndef SYS_MACROS_H
#define SYS_MACROS_H

#include <stddef.h>

#define ARRAY_SIZE(array)	(sizeof(array) / sizeof((array)[0]))

#define ALIGN_DOWN(value, alignment)	((value) & ~((__typeof__(value))(alignment) - 1))
#define ALIGN_UP(value, alignment)	ALIGN_DOWN((value) + (alignment) - 1, alignment)

#define container_of(pointer, type, member) \
	((type *)((char *)(pointer) - offsetof(type, member)))

#endif
