#include <arch.h>
#include <errno.h>
#include <lib/blockdev.h>
#include <lib/fs.h>
#include <lib/heap.h>
#include <lib/memmap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/console.h>
#include <sys/format.h>
#include <sys/macros.h>
#include <sys/mem.h>
#include <sys/time.h>

#include "private.h"

#define MEMORY_LIMIT		(256ULL << 20)
#define MEMORY_STEP		(16ULL << 20)
#define RANGES_MAX		32
#define CHUNK_SIZE		(1024 * 1024)
#define WALK_DEPTH_MAX		16
#define LISTING_INITIAL		16
#define PATTERN			0x5aa5c33c0ff0e11eULL
#define WORD_SIZE		sizeof(uint64_t)
#define OWN_ADDRESS_SWEEPS	4

struct range {
	uint64_t	base;
	uint64_t	size;
};

struct memory {
	struct range	ranges[RANGES_MAX];
	size_t		count;
	uint64_t	total;
};

struct sweep {
	bool	inverted;
	bool	descending;
	bool	checking;
};

struct listing {
	struct fs_entry	*entries;
	size_t		count;
	size_t		capacity;
};

struct walk {
	struct fs	*fs;
	uint8_t		*buffer;
	uint64_t	files;
	uint64_t	directories;
	uint64_t	bytes;
	char		path[FS_PATH_MAX];
};

struct outcome {
	unsigned int	problems;
	bool		stopped;
};

static const char owner_diagnostics[] = "diagnostics";

static const struct sweep own_address_sweeps[OWN_ADDRESS_SWEEPS] = {
	{ .inverted = false, .descending = false, .checking = false },
	{ .inverted = false, .descending = false, .checking = true },
	{ .inverted = true, .descending = true, .checking = false },
	{ .inverted = true, .descending = true, .checking = true },
};

static bool interrupted(void)
{
	return console_poll() >= 0;
}

static void record(struct outcome *outcome, int err)
{
	if (err == -ECANCELED) {
		outcome->stopped = true;
	} else if (err != 0) {
		outcome->problems++;
	}
}

static int mismatch(const char *test, uint64_t address, uint64_t expected, uint64_t found)
{
	printf("%s %#lx wrote %016lx read %016lx bits %016lx failed\n", test, address, expected, found,
	       expected ^ found);

	return -EIO;
}

static uint64_t load_uncached(uint64_t address)
{
	arch_dcache_clean_invalidate_range((uintptr_t)address, WORD_SIZE);

	return *(volatile uint64_t *)(uintptr_t)address;
}

static void store_through(uint64_t address, uint64_t value)
{
	*(volatile uint64_t *)(uintptr_t)address = value;
	arch_dcache_clean_invalidate_range((uintptr_t)address, WORD_SIZE);
}

static void collect_free(struct memory *memory)
{
	const struct memmap *map = mem_map();
	size_t i;

	memset(memory, 0, sizeof(*memory));

	for (i = 0; i < map->count && memory->count < RANGES_MAX && memory->total < MEMORY_LIMIT; i++) {
		const struct memmap_segment *segment = &map->segments[i];
		uint64_t size = segment->end - segment->base;

		if (segment->owner != NULL) {
			continue;
		}

		if (size > MEMORY_LIMIT - memory->total) {
			size = MEMORY_LIMIT - memory->total;
		}

		memory->ranges[memory->count].base = segment->base;
		memory->ranges[memory->count].size = size;
		memory->count++;
		memory->total += size;
	}
}

static int claim(const struct memory *memory)
{
	size_t i;

	for (i = 0; i < memory->count; i++) {
		int err = mem_claim(memory->ranges[i].base, memory->ranges[i].size, owner_diagnostics);

		if (err != 0) {
			return err;
		}
	}

	return 0;
}

static int test_data_lines(uint64_t address)
{
	unsigned int bit;

	for (bit = 0; bit < 64; bit++) {
		uint64_t values[] = { 1ULL << bit, ~(1ULL << bit) };
		size_t i;

		for (i = 0; i < ARRAY_SIZE(values); i++) {
			uint64_t found;

			store_through(address, values[i]);
			found = load_uncached(address);
			if (found != values[i]) {
				return mismatch("data lines", address, values[i], found);
			}
		}
	}

	return 0;
}

static int check_probes(const struct range *range, uint64_t skipped)
{
	uint64_t offset;

	for (offset = WORD_SIZE; offset < range->size; offset <<= 1) {
		uint64_t found = load_uncached(range->base + offset);

		if (offset != skipped && found != PATTERN) {
			return mismatch("address lines", range->base + offset, PATTERN, found);
		}
	}

	return 0;
}

static int test_address_lines(const struct range *range)
{
	uint64_t offset;
	uint64_t found;
	int err;

	for (offset = WORD_SIZE; offset < range->size; offset <<= 1) {
		store_through(range->base + offset, PATTERN);
	}

	store_through(range->base, ~PATTERN);
	err = check_probes(range, 0);
	store_through(range->base, PATTERN);

	for (offset = WORD_SIZE; err == 0 && offset < range->size; offset <<= 1) {
		store_through(range->base + offset, ~PATTERN);

		found = load_uncached(range->base);
		if (found != PATTERN) {
			return mismatch("address lines", range->base, PATTERN, found);
		}

		err = check_probes(range, offset);
		store_through(range->base + offset, PATTERN);
	}

	return err;
}

static uint64_t own_value(uint64_t address, bool inverted)
{
	return inverted ? ~(address ^ PATTERN) : address ^ PATTERN;
}

static int sweep_step(uint64_t start, uint64_t length, const struct sweep *sweep)
{
	volatile uint64_t *words = (volatile uint64_t *)(uintptr_t)start;
	uint64_t count = length / WORD_SIZE;
	uint64_t i;

	for (i = 0; i < count; i++) {
		uint64_t index = sweep->descending ? count - 1 - i : i;
		uint64_t address = start + index * WORD_SIZE;
		uint64_t expected = own_value(address, sweep->inverted);
		uint64_t found;

		if (!sweep->checking) {
			words[index] = expected;
			continue;
		}

		found = words[index];
		if (found != expected) {
			return mismatch("own address", address, expected, found);
		}
	}

	if (!sweep->checking) {
		arch_dcache_clean_invalidate_range((uintptr_t)start, (size_t)length);
	}

	return 0;
}

static int sweep_memory(const struct memory *memory, const struct sweep *sweep)
{
	size_t r;

	for (r = 0; r < memory->count; r++) {
		const struct range *range = &memory->ranges[sweep->descending ? memory->count - 1 - r : r];
		uint64_t done;

		for (done = 0; done < range->size;) {
			uint64_t length = range->size - done < MEMORY_STEP ? range->size - done : MEMORY_STEP;
			uint64_t start = sweep->descending ? range->base + range->size - done - length : range->base + done;
			int err = sweep_step(start, length, sweep);

			if (err != 0) {
				return err;
			}

			done += length;

			if (interrupted()) {
				return -ECANCELED;
			}
		}
	}

	return 0;
}

static int test_own_address(const struct memory *memory)
{
	uint64_t start = time_us();
	char size[SIZE_TEXT];
	size_t i;
	int err = 0;

	for (i = 0; i < ARRAY_SIZE(own_address_sweeps) && err == 0; i++) {
		err = sweep_memory(memory, &own_address_sweeps[i]);
	}

	if (err == -ECANCELED) {
		printf("own address stopped\n");
	} else if (err == 0) {
		format_size(size, sizeof(size), memory->total);
		printf("own address %s written and read twice in %lu ms ok\n", size,
		       (time_us() - start) / USEC_PER_MSEC);
	}

	return err;
}

static int test_memory(const struct memory *memory)
{
	size_t i;
	int err = test_data_lines(memory->ranges[0].base);

	if (err != 0) {
		return err;
	}

	printf("data lines ok\n");

	for (i = 0; i < memory->count && err == 0; i++) {
		err = test_address_lines(&memory->ranges[i]);
	}

	if (err != 0) {
		return err;
	}

	printf("address lines ok\n");

	return test_own_address(memory);
}

static int check_memory(void)
{
	struct memory memory;
	char size[SIZE_TEXT];
	int err;

	collect_free(&memory);
	if (memory.count == 0) {
		printf("memory nothing free to test\n");
		return -ENOMEM;
	}

	format_size(size, sizeof(size), memory.total);
	printf("memory test %s in %zu %s\n", size, memory.count, memory.count == 1 ? "range" : "ranges");

	err = claim(&memory);
	if (err == 0) {
		err = test_memory(&memory);
	} else {
		printf("memory claim %s\n", strerror(-err));
	}

	mem_release_owner(owner_diagnostics);

	return err;
}

static int check_disk(struct blockdev *disk, uint8_t *buffer)
{
	uint32_t chunk = CHUNK_SIZE / disk->block_size;
	uint64_t start = time_us();
	uint64_t block;
	char size[SIZE_TEXT];
	int err = 0;

	for (block = 0; block < disk->block_count && err == 0; block += chunk) {
		uint32_t count = disk->block_count - block < chunk ? (uint32_t)(disk->block_count - block) : chunk;

		err = blockdev_read(disk, block, count, buffer);
		if (err != 0) {
			printf("%s block %lu %s\n", disk->name, block, strerror(-err));
			return err;
		}

		if (interrupted()) {
			err = -ECANCELED;
		}
	}

	format_size(size, sizeof(size), blockdev_size(disk));
	printf("%s %s read in %lu ms %s\n", disk->name, size, (time_us() - start) / USEC_PER_MSEC,
	       err == 0 ? "ok" : "stopped");

	return err;
}

static void check_disks(struct outcome *outcome, uint8_t *buffer)
{
	struct blockdev *disk;
	unsigned int count = 0;

	for (disk = blockdev_list(); disk != NULL; disk = disk->next) {
		if (disk->parent == NULL) {
			record(outcome, check_disk(disk, buffer));
			count++;
		}
	}

	if (count == 0) {
		printf("disks none\n");
	}
}

static int collect_entry(void *context, const struct fs_entry *entry)
{
	struct listing *listing = context;

	if (listing->count == listing->capacity) {
		size_t capacity = listing->capacity == 0 ? LISTING_INITIAL : listing->capacity * 2;
		struct fs_entry *entries = realloc(listing->entries, capacity * sizeof(*entries));

		if (entries == NULL) {
			return -ENOMEM;
		}

		listing->entries = entries;
		listing->capacity = capacity;
	}

	listing->entries[listing->count++] = *entry;

	return 0;
}

static int read_file(struct walk *walk, uint64_t size)
{
	uint64_t offset;

	for (offset = 0; offset < size;) {
		size_t length = size - offset < CHUNK_SIZE ? (size_t)(size - offset) : CHUNK_SIZE;
		size_t done;
		int err = fs_read(walk->fs, walk->path, offset, walk->buffer, length, &done);

		if (err != 0) {
			return err;
		}

		if (done != length) {
			return -EIO;
		}

		offset += done;
		walk->bytes += done;
	}

	return interrupted() ? -ECANCELED : 0;
}

static int walk_directory(struct walk *walk, size_t length, unsigned int depth)
{
	struct listing listing = { NULL, 0, 0 };
	size_t i;
	int err;

	if (depth > WALK_DEPTH_MAX) {
		return -ELOOP;
	}

	err = fs_list(walk->fs, length == 0 ? "/" : walk->path, collect_entry, &listing);

	for (i = 0; i < listing.count && err == 0; i++) {
		const struct fs_entry *entry = &listing.entries[i];
		size_t extended = length + 1 + strlen(entry->name);

		if (extended >= sizeof(walk->path)) {
			err = -ENAMETOOLONG;
			break;
		}

		walk->path[length] = '/';
		strlcpy(walk->path + length + 1, entry->name, sizeof(walk->path) - length - 1);

		if (entry->kind == FS_KIND_DIRECTORY) {
			walk->directories++;
			err = walk_directory(walk, extended, depth + 1);
		} else if (entry->kind == FS_KIND_FILE) {
			walk->files++;
			err = read_file(walk, entry->size);
		}

		if (err == 0) {
			walk->path[length] = '\0';
		}
	}

	free(listing.entries);

	return err;
}

static int check_filesystem(struct blockdev *device, struct walk *walk)
{
	const char *label = fs_label(walk->fs);
	char size[SIZE_TEXT];
	int err;

	walk->files = 0;
	walk->directories = 0;
	walk->bytes = 0;
	walk->path[0] = '\0';

	err = walk_directory(walk, 0, 0);

	format_size(size, sizeof(size), walk->bytes);
	printf("%s %s", device->name, fs_type(walk->fs));
	if (label[0] != '\0') {
		printf(" '%s'", label);
	}
	printf(", %lu files, %lu directories, %s read ", walk->files, walk->directories, size);

	if (err == 0) {
		printf("ok\n");
	} else if (err == -ECANCELED) {
		printf("stopped\n");
	} else {
		printf("%s at %s\n", strerror(-err), walk->path[0] != '\0' ? walk->path : "/");
	}

	return err;
}

static void check_filesystems(struct outcome *outcome, uint8_t *buffer)
{
	struct walk *walk = malloc(sizeof(*walk));
	struct blockdev *device;
	unsigned int count = 0;

	if (walk == NULL) {
		printf("filesystems %s\n", strerror(ENOMEM));
		record(outcome, -ENOMEM);
		return;
	}

	walk->buffer = buffer;

	for (device = blockdev_list(); device != NULL; device = device->next) {
		if (fs_mount(device, &walk->fs) != 0) {
			continue;
		}

		record(outcome, check_filesystem(device, walk));
		fs_unmount(walk->fs);
		count++;
	}

	if (count == 0) {
		printf("filesystems none\n");
	}

	free(walk);
}

static void check_heap(struct outcome *outcome)
{
	struct heap *heap = mem_heap();
	int err = heap != NULL ? heap_check(heap) : -ENODEV;

	printf("heap %s\n", err == 0 ? "ok" : strerror(-err));
	record(outcome, err);
}

int diagnostics_run(void)
{
	struct outcome outcome = { 0, false };
	uint8_t *buffer;

	printf("diagnostics running, any key stops\n");

	record(&outcome, check_memory());

	buffer = malloc(CHUNK_SIZE);
	if (buffer != NULL) {
		check_disks(&outcome, buffer);
		check_filesystems(&outcome, buffer);
		free(buffer);
	} else {
		printf("disks %s\n", strerror(ENOMEM));
		record(&outcome, -ENOMEM);
	}

	check_heap(&outcome);

	if (outcome.problems > 0) {
		printf("diagnostics found %u %s\n", outcome.problems, outcome.problems == 1 ? "problem" : "problems");
		return -EIO;
	}

	printf("diagnostics %s\n", outcome.stopped ? "were stopped" : "passed");

	return outcome.stopped ? -ECANCELED : 0;
}
