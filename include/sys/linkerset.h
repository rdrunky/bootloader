#ifndef SYS_LINKERSET_H
#define SYS_LINKERSET_H

#include <compiler.h>
#include <stdbool.h>

#define SET_DECLARE(set, type) \
	extern type *const __set_##set##_start[]; \
	extern type *const __set_##set##_stop[]

#define SET_ENTRY(set, symbol) \
	static __typeof__(&symbol) const __set_##set##_##symbol __section(".set." #set) __used = &(symbol)

#define SET_FOREACH(item, set) \
	for (__typeof__(&__set_##set##_start[0]) __cursor = __set_##set##_start; \
	     __cursor < __set_##set##_stop && ((item) = *__cursor, true); \
	     __cursor++)

#endif
