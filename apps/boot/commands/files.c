#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/format.h>
#include <sys/mem.h>
#include <sys/shell.h>

#include "../private.h"

#define LIST_MAX	4096
#define CAT_MAX		(1024 * 1024)
#define HASH_CHUNK	(1024 * 1024)
#define LOAD_ALIGN	(2ULL << 20)

struct listing {
	struct fs_entry	*entries;
	size_t		count;
	bool		truncated;
};

static const char owner_loaded[] = "loaded";

static int report(const char *command, const char *spec, int err)
{
	printf("%s: %s: %s\n", command, spec, strerror(-err));

	return -ECANCELED;
}

static const char *kind_name(enum fs_kind kind)
{
	switch (kind) {
	case FS_KIND_FILE:
		return "file";
	case FS_KIND_DIRECTORY:
		return "directory";
	case FS_KIND_SYMLINK:
		return "link";
	default:
		return "other";
	}
}

static int collect(void *context, const struct fs_entry *entry)
{
	struct listing *listing = context;

	if (listing->count == LIST_MAX) {
		listing->truncated = true;
		return 1;
	}

	listing->entries[listing->count++] = *entry;

	return 0;
}

static int compare_entries(const void *a, const void *b)
{
	return strcmp(((const struct fs_entry *)a)->name, ((const struct fs_entry *)b)->name);
}

static void print_entry(const struct fs_entry *entry)
{
	char size[SIZE_TEXT];

	format_size(size, sizeof(size), entry->size);
	printf("  %-40s %10s  %s\n", entry->name, entry->kind == FS_KIND_DIRECTORY ? "" : size,
	       kind_name(entry->kind));
}

static int list_directory(struct location *location)
{
	struct listing listing = { .entries = malloc(LIST_MAX * sizeof(struct fs_entry)) };
	size_t i;
	int err;

	if (listing.entries == NULL) {
		return -ENOMEM;
	}

	err = fs_list(location->fs, location->path, collect, &listing);
	if (err >= 0) {
		qsort(listing.entries, listing.count, sizeof(listing.entries[0]), compare_entries);
		for (i = 0; i < listing.count; i++) {
			print_entry(&listing.entries[i]);
		}

		if (listing.truncated) {
			printf("  (only the first %d entries are shown)\n", LIST_MAX);
		}

		err = 0;
	}

	free(listing.entries);

	return err;
}

COMMAND(ls, "device:/path", "list a directory")
{
	struct location location;
	struct fs_stat stat;
	int err;

	if (argc != 2) {
		return SHELL_USAGE;
	}

	err = location_open(argv[1], &location);
	if (err != 0) {
		return report(argv[0], argv[1], err);
	}

	err = fs_stat(location.fs, location.path, &stat);
	if (err == 0 && stat.kind == FS_KIND_DIRECTORY) {
		err = list_directory(&location);
	} else if (err == 0) {
		struct fs_entry entry = { .kind = stat.kind, .size = stat.size };

		strlcpy(entry.name, location.path, sizeof(entry.name));
		print_entry(&entry);
	}

	location_close(&location);

	return err != 0 ? report(argv[0], argv[1], err) : 0;
}

static void print_text(const uint8_t *data, size_t length)
{
	size_t i;

	for (i = 0; i < length; i++) {
		putchar(isprint(data[i]) || data[i] == '\n' || data[i] == '\t' ? data[i] : '.');
	}

	if (length > 0 && data[length - 1] != '\n') {
		putchar('\n');
	}
}

COMMAND(cat, "device:/path", "print a text file")
{
	struct location location;
	struct fs_stat stat;
	uint8_t *data = NULL;
	size_t done = 0;
	int err;

	if (argc != 2) {
		return SHELL_USAGE;
	}

	err = location_open(argv[1], &location);
	if (err != 0) {
		return report(argv[0], argv[1], err);
	}

	err = fs_stat(location.fs, location.path, &stat);
	if (err == 0 && stat.kind == FS_KIND_DIRECTORY) {
		err = -EISDIR;
	}

	if (err == 0) {
		data = malloc(CAT_MAX);
		err = data != NULL ? fs_read(location.fs, location.path, 0, data, CAT_MAX, &done) : -ENOMEM;
	}

	if (err == 0) {
		print_text(data, done);
		if (stat.size > CAT_MAX) {
			printf("(only the first %d bytes are shown)\n", CAT_MAX);
		}
	}

	free(data);
	location_close(&location);

	return err != 0 ? report(argv[0], argv[1], err) : 0;
}

COMMAND(sha256, "device:/path", "print the sha256 of a file")
{
	char text[SHA256_SIZE * 2 + 1];
	uint8_t digest[SHA256_SIZE];
	struct location location;
	struct sha256 context;
	struct fs_stat stat;
	uint8_t *chunk;
	uint64_t offset = 0;
	int err;

	if (argc != 2) {
		return SHELL_USAGE;
	}

	err = location_open(argv[1], &location);
	if (err != 0) {
		return report(argv[0], argv[1], err);
	}

	err = fs_stat(location.fs, location.path, &stat);
	if (err == 0 && stat.kind != FS_KIND_FILE) {
		err = -EISDIR;
	}

	chunk = err == 0 ? malloc(HASH_CHUNK) : NULL;
	if (err == 0 && chunk == NULL) {
		err = -ENOMEM;
	}

	sha256_init(&context);
	while (err == 0 && offset < stat.size) {
		size_t done;

		err = fs_read(location.fs, location.path, offset, chunk, HASH_CHUNK, &done);
		if (err == 0 && done == 0) {
			err = -EIO;
		}

		if (err == 0) {
			sha256_update(&context, chunk, done);
			offset += done;
		}
	}

	free(chunk);
	location_close(&location);

	if (err != 0) {
		return report(argv[0], argv[1], err);
	}

	sha256_final(&context, digest);
	format_digest(digest, text);
	printf("%s  %s\n", text, argv[1]);

	return 0;
}

static int reserve_destination(bool fixed, uint64_t size, uint64_t *address)
{
	uint64_t length = size == 0 ? 1 : size;
	int err = fixed ? mem_claim(*address, length, owner_loaded) :
			  mem_allocate(length, LOAD_ALIGN, owner_loaded, address);

	if (err != 0 && fixed) {
		printf("load: no room at %#lx for %lu bytes: %s\n", *address, size, strerror(-err));
	} else if (err != 0) {
		printf("load: no room for %lu bytes: %s\n", size, strerror(-err));
	}

	return err;
}

DEVELOPER_COMMAND(load, "device:/path [address]", "load a file into memory")
{
	struct location location;
	struct fs_stat stat;
	uint64_t address = 0;
	size_t done = 0;
	int err;

	if ((argc != 2 && argc != 3) || (argc == 3 && parse_number(argv[2], &address) != 0)) {
		return SHELL_USAGE;
	}

	err = location_open(argv[1], &location);
	if (err != 0) {
		return report(argv[0], argv[1], err);
	}

	err = fs_stat(location.fs, location.path, &stat);
	if (err == 0 && stat.kind != FS_KIND_FILE) {
		err = -EISDIR;
	}

	if (err == 0 && reserve_destination(argc == 3, stat.size, &address) != 0) {
		location_close(&location);
		return -ECANCELED;
	}

	if (err == 0) {
		err = fs_read(location.fs, location.path, 0, (void *)(uintptr_t)address, (size_t)stat.size, &done);
	}

	if (err == 0 && done != stat.size) {
		err = -EIO;
	}

	location_close(&location);

	if (err != 0) {
		return report(argv[0], argv[1], err);
	}

	printf("loaded %lu bytes at %#lx\n", (uint64_t)done, address);

	return 0;
}
