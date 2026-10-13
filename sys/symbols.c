#include <arch.h>
#include <stddef.h>
#include <sys/symbols.h>

struct symbol {
	uint32_t	offset;
	uint32_t	name;
};

extern const uint32_t __symbol_count;
extern const struct symbol __symbols[];
extern const char __symbol_names[];
extern const char __image_start[];
extern const char __text_end[];

const char *symbol_lookup(uintptr_t address, uintptr_t *offset)
{
	uintptr_t relative = address - (uintptr_t)__image_start;
	uint32_t low = 0;
	uint32_t high = __symbol_count;

	if (address < (uintptr_t)__image_start || address >= (uintptr_t)__text_end || high == 0) {
		return NULL;
	}

	while (high - low > 1) {
		uint32_t middle = low + (high - low) / 2;

		if (__symbols[middle].offset <= relative) {
			low = middle;
		} else {
			high = middle;
		}
	}

	if (__symbols[low].offset > relative) {
		return NULL;
	}

	*offset = relative - __symbols[low].offset;

	return __symbol_names + __symbols[low].name;
}
