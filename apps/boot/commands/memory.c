#include <arch.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/console.h>
#include <sys/format.h>
#include <sys/mem.h>
#include <sys/shell.h>

#include "../private.h"

#define LINE_BYTES	16
#define DUMP_DEFAULT	64
#define DUMP_MAX	0x100000
#define FILL_MAX	0x100000
#define KEY_INTERRUPT	0x03

static unsigned int parse_width(const char *option)
{
	if (strcmp(option, "-b") == 0) {
		return 1;
	}

	if (strcmp(option, "-h") == 0) {
		return 2;
	}

	if (strcmp(option, "-w") == 0) {
		return 4;
	}

	if (strcmp(option, "-q") == 0) {
		return 8;
	}

	return 0;
}

static int take_width(int argc, char **argv, int *index, unsigned int *width)
{
	*index = 1;
	*width = 1;

	if (argc > 1 && argv[1][0] == '-') {
		*width = parse_width(argv[1]);
		*index = 2;
	}

	return *width != 0 ? 0 : SHELL_USAGE;
}

static bool fits_width(uint64_t value, unsigned int width)
{
	return width == 8 || value < (1ULL << (width * 8));
}

static void print_line(uint64_t address, const uint8_t *bytes, unsigned int count, unsigned int width)
{
	unsigned int i;

	printf("%016lx:", address);
	for (i = 0; i < count; i += width) {
		uint64_t value = 0;

		memcpy(&value, bytes + i, width);
		printf(" %0*lx", (int)(width * 2), value);
	}

	if (width == 1) {
		printf("%*s  |", (int)((LINE_BYTES - count) * 3), "");
		for (i = 0; i < count; i++) {
			putchar(isprint(bytes[i]) ? bytes[i] : '.');
		}
		printf("|");
	}

	printf("\n");
}

static int dump(uint64_t address, uint64_t length, unsigned int width)
{
	uint64_t offset;

	for (offset = 0; offset < length; offset += LINE_BYTES) {
		uint8_t bytes[LINE_BYTES];
		unsigned int count = length - offset < LINE_BYTES ? (unsigned int)(length - offset) : LINE_BYTES;
		unsigned int i;

		for (i = 0; i < count; i += width) {
			uint64_t value;
			int err = arch_probe_read(address + offset + i, width, &value);

			if (err != 0) {
				printf("md: %#lx: %s\n", address + offset + i, strerror(-err));
				return -ECANCELED;
			}

			memcpy(bytes + i, &value, width);
		}

		print_line(address + offset, bytes, count, width);

		if (console_poll() == KEY_INTERRUPT) {
			return -ECANCELED;
		}
	}

	return 0;
}

DEVELOPER_COMMAND(md, "[-b|-h|-w|-q] address [length]", "display memory, bad addresses are reported")
{
	uint64_t length = DUMP_DEFAULT;
	uint64_t address;
	unsigned int width;
	int index;

	if (take_width(argc, argv, &index, &width) != 0 || argc - index < 1 || argc - index > 2) {
		return SHELL_USAGE;
	}

	if (parse_number(argv[index], &address) != 0) {
		return SHELL_USAGE;
	}

	if (argc - index == 2 && (parse_number(argv[index + 1], &length) != 0 || length == 0)) {
		return SHELL_USAGE;
	}

	if (address % width != 0 || length > DUMP_MAX) {
		return -EINVAL;
	}

	return dump(address, (length + width - 1) / width * width, width);
}

DEVELOPER_COMMAND(mw, "[-b|-h|-w|-q] address value [count]", "write memory, bad addresses are reported")
{
	uint64_t count = 1;
	uint64_t address;
	uint64_t value;
	uint64_t i;
	unsigned int width;
	int index;

	if (take_width(argc, argv, &index, &width) != 0 || argc - index < 2 || argc - index > 3) {
		return SHELL_USAGE;
	}

	if (parse_number(argv[index], &address) != 0 || parse_number(argv[index + 1], &value) != 0 ||
	    (argc - index == 3 && (parse_number(argv[index + 2], &count) != 0 || count == 0))) {
		return SHELL_USAGE;
	}

	if (address % width != 0 || !fits_width(value, width) || count > FILL_MAX) {
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		int err = arch_probe_write(address + i * width, width, value);

		if (err != 0) {
			printf("mw: %#lx: %s\n", address + i * width, strerror(-err));
			return -ECANCELED;
		}
	}

	return 0;
}

COMMAND(mem, "", "show the memory map")
{
	const struct memmap *map = mem_map();
	size_t i;

	if (argc != 1) {
		return SHELL_USAGE;
	}

	for (i = 0; i < map->count; i++) {
		const struct memmap_segment *segment = &map->segments[i];
		char size[SIZE_TEXT];

		format_size(size, sizeof(size), segment->end - segment->base);
		printf("  %016lx-%016lx  %10s  %s\n", segment->base, segment->end, size,
		       segment->owner != NULL ? segment->owner : "free");
	}

	return 0;
}

COMMAND(heap, "", "show heap usage and check its integrity")
{
	struct heap *heap = mem_heap();
	struct heap_stats stats;
	char size[SIZE_TEXT];
	char used[SIZE_TEXT];
	char peak[SIZE_TEXT];
	char largest[SIZE_TEXT];
	int err;

	if (argc != 1) {
		return SHELL_USAGE;
	}

	if (heap == NULL) {
		return -ENODEV;
	}

	heap_stats(heap, &stats);
	err = heap_check(heap);

	format_size(size, sizeof(size), stats.size);
	format_size(used, sizeof(used), stats.used);
	format_size(peak, sizeof(peak), stats.peak);
	format_size(largest, sizeof(largest), stats.largest_free);

	printf("  size %s, used %s, peak %s\n", size, used, peak);
	printf("  %zu allocations, %zu free blocks, largest %s\n", stats.allocations, stats.free_blocks, largest);
	printf("  integrity %s\n", err == 0 ? "ok" : strerror(-err));

	return err == 0 ? 0 : -ECANCELED;
}
