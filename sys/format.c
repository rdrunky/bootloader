#include <stdio.h>
#include <sys/format.h>
#include <sys/macros.h>

void format_size(char *buffer, size_t length, uint64_t bytes)
{
	static const char *const units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
	uint64_t scale = 1;
	unsigned int unit = 0;

	while (unit + 1 < ARRAY_SIZE(units) && bytes / scale >= 1024) {
		scale *= 1024;
		unit++;
	}

	if (bytes % scale == 0) {
		snprintf(buffer, length, "%lu %s", bytes / scale, units[unit]);
	} else {
		snprintf(buffer, length, "%lu.%lu %s", bytes / scale, bytes % scale * 10 / scale, units[unit]);
	}
}
