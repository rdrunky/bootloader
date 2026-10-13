#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>

#include "private.h"

#define ENTRY_NAME		0
#define ENTRY_EXTENSION		8
#define ENTRY_ATTRIBUTE		11
#define ENTRY_CASE		12
#define ENTRY_CLUSTER_HIGH	20
#define ENTRY_CLUSTER_LOW	26
#define ENTRY_SIZE		28

#define LONG_ORDER		0
#define LONG_CHECKSUM		13
#define LONG_LAST		0x40
#define LONG_ORDER_MASK		0x1f
#define LONG_UNITS		13
#define LONG_ENTRIES_MAX	20

#define ATTRIBUTE_VOLUME	0x08
#define ATTRIBUTE_DIRECTORY	0x10
#define ATTRIBUTE_LONG		0x0f
#define ATTRIBUTE_LONG_MASK	0x3f
#define CASE_LOWER_BASE		0x08
#define CASE_LOWER_EXTENSION	0x10
#define SHORT_NAME_SIZE		11
#define SHORT_PADDING		' '

#define MARK_END		0x00
#define MARK_DELETED		0xe5
#define MARK_KANJI		0x05

#define NAME_MAX_LENGTH		255
#define PATH_DEPTH_MAX		64
#define DOT_DOT			".."

struct long_name {
	uint16_t	units[LONG_ENTRIES_MAX * LONG_UNITS];
	unsigned int	expected;
	unsigned int	entries;
	uint8_t		checksum;
};

struct scan {
	struct long_name	long_name;
	fat_visit		visit;
	void			*context;
	bool			finished;
	char			name[NAME_MAX_LENGTH + 1];
};

static const unsigned int unit_offsets[LONG_UNITS] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };

static uint8_t short_checksum(const uint8_t *name)
{
	uint8_t sum = 0;
	unsigned int i;

	for (i = 0; i < SHORT_NAME_SIZE; i++) {
		sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + name[i]);
	}

	return sum;
}

static void collect_long(struct long_name *long_name, const uint8_t *entry)
{
	unsigned int order = entry[LONG_ORDER] & LONG_ORDER_MASK;
	unsigned int i;

	if ((entry[LONG_ORDER] & LONG_LAST) != 0) {
		long_name->entries = order;
		long_name->expected = order;
		long_name->checksum = entry[LONG_CHECKSUM];
	}

	if (order == 0 || order > LONG_ENTRIES_MAX || order != long_name->expected ||
	    entry[LONG_CHECKSUM] != long_name->checksum) {
		long_name->entries = 0;
		long_name->expected = 0;
		return;
	}

	for (i = 0; i < LONG_UNITS; i++) {
		long_name->units[(order - 1) * LONG_UNITS + i] = get_le16(entry + unit_offsets[i]);
	}

	long_name->expected--;
}

static bool take_long_name(struct long_name *long_name, const uint8_t *entry, char *name)
{
	size_t length = 0;
	size_t i;

	if (long_name->entries == 0 || long_name->expected != 0 || long_name->checksum != short_checksum(entry)) {
		return false;
	}

	for (i = 0; i < (size_t)long_name->entries * LONG_UNITS && long_name->units[i] != 0; i++) {
		uint16_t unit = long_name->units[i];

		if (length == NAME_MAX_LENGTH) {
			return false;
		}

		name[length++] = unit >= 0x20 && unit < 0x7f && unit != '/' ? (char)unit : '?';
	}

	name[length] = '\0';

	return length > 0;
}

static void short_name(const uint8_t *entry, char *name)
{
	bool lower_base = (entry[ENTRY_CASE] & CASE_LOWER_BASE) != 0;
	bool lower_extension = (entry[ENTRY_CASE] & CASE_LOWER_EXTENSION) != 0;
	size_t length = 0;
	size_t i;

	for (i = ENTRY_NAME; i < ENTRY_EXTENSION && entry[i] != SHORT_PADDING; i++) {
		int c = i == ENTRY_NAME && entry[i] == MARK_KANJI ? MARK_DELETED : entry[i];

		name[length++] = (char)(lower_base ? tolower(c) : c);
	}

	if (entry[ENTRY_EXTENSION] != SHORT_PADDING) {
		name[length++] = '.';
	}

	for (i = ENTRY_EXTENSION; i < SHORT_NAME_SIZE && entry[i] != SHORT_PADDING; i++) {
		name[length++] = (char)(lower_extension ? tolower(entry[i]) : entry[i]);
	}

	name[length] = '\0';
}

static int scan_entry(struct fat *fat, struct scan *scan, const uint8_t *entry)
{
	uint8_t attribute = entry[ENTRY_ATTRIBUTE];
	struct fat_node node;

	if (entry[ENTRY_NAME] == MARK_END) {
		scan->finished = true;
		return 0;
	}

	if (entry[ENTRY_NAME] == MARK_DELETED) {
		scan->long_name.entries = 0;
		return 0;
	}

	if ((attribute & ATTRIBUTE_LONG_MASK) == ATTRIBUTE_LONG) {
		collect_long(&scan->long_name, entry);
		return 0;
	}

	if ((attribute & ATTRIBUTE_VOLUME) != 0) {
		scan->long_name.entries = 0;
		return 0;
	}

	if (!take_long_name(&scan->long_name, entry, scan->name)) {
		short_name(entry, scan->name);
	}

	scan->long_name.entries = 0;
	node.directory = (attribute & ATTRIBUTE_DIRECTORY) != 0;
	node.cluster = get_le16(entry + ENTRY_CLUSTER_LOW);
	if (fat->bits == 32) {
		node.cluster |= (uint32_t)get_le16(entry + ENTRY_CLUSTER_HIGH) << 16;
	}

	node.size = get_le32(entry + ENTRY_SIZE);
	node.root = node.directory && node.cluster == 0;

	return scan->visit(scan->context, scan->name, &node);
}

static int scan_block(struct fat *fat, struct scan *scan, const uint8_t *block, size_t size)
{
	size_t offset;

	for (offset = 0; offset + FAT_ENTRY_SIZE <= size && !scan->finished; offset += FAT_ENTRY_SIZE) {
		int stop = scan_entry(fat, scan, block + offset);

		if (stop != 0) {
			return stop;
		}
	}

	return 0;
}

static int scan_fixed_root(struct fat *fat, struct scan *scan, uint8_t *block)
{
	uint64_t total = (uint64_t)fat->root_entries * FAT_ENTRY_SIZE;
	uint64_t offset;
	int result = 0;

	for (offset = 0; offset < total && result == 0 && !scan->finished; offset += fat->cluster_size) {
		size_t size = total - offset < fat->cluster_size ? (size_t)(total - offset) : fat->cluster_size;

		result = blockdev_read_bytes(fat->device, fat->root_offset + offset, block, size);
		if (result == 0) {
			result = scan_block(fat, scan, block, size);
		}
	}

	return result;
}

static int scan_chain(struct fat *fat, struct scan *scan, uint32_t cluster, uint8_t *block)
{
	uint32_t steps = 0;
	int result = 0;

	while (cluster != 0 && result == 0 && !scan->finished) {
		if (++steps > fat->cluster_count) {
			return -EBADMSG;
		}

		result = blockdev_read_bytes(fat->device, fat_cluster_offset(fat, cluster), block, fat->cluster_size);
		if (result == 0) {
			result = scan_block(fat, scan, block, fat->cluster_size);
		}

		if (result == 0) {
			result = fat_next(fat, cluster, &cluster);
		}
	}

	return result;
}

int fat_iterate(struct fat *fat, const struct fat_node *directory, fat_visit visit, void *context)
{
	struct scan *scan = calloc(1, sizeof(*scan));
	uint8_t *block = malloc(fat->cluster_size);
	int result;

	if (scan == NULL || block == NULL) {
		result = -ENOMEM;
	} else {
		scan->visit = visit;
		scan->context = context;

		if (directory->root && fat->bits != 32) {
			result = scan_fixed_root(fat, scan, block);
		} else {
			result = scan_chain(fat, scan, directory->root ? fat->root_cluster : directory->cluster, block);
		}
	}

	free(block);
	free(scan);

	return result;
}

struct search {
	const char	*name;
	size_t		length;
	struct fat_node	node;
	bool		found;
};

static int match_name(void *context, const char *name, const struct fat_node *node)
{
	struct search *search = context;

	if (strlen(name) != search->length || strncasecmp(name, search->name, search->length) != 0) {
		return 0;
	}

	search->node = *node;
	search->found = true;

	return 1;
}

static int step_into(struct fat *fat, struct fat_node *current, const char *name, size_t length)
{
	struct search search = { .name = name, .length = length };
	int result;

	if (!current->directory) {
		return -ENOTDIR;
	}

	if (current->root && length == 2 && memcmp(name, DOT_DOT, 2) == 0) {
		return 0;
	}

	result = fat_iterate(fat, current, match_name, &search);
	if (result < 0) {
		return result;
	}

	if (!search.found) {
		return -ENOENT;
	}

	*current = search.node;

	return 0;
}

int fat_lookup(struct fat *fat, const char *path, struct fat_node *node)
{
	unsigned int depth = 0;

	*node = (struct fat_node){ .directory = true, .root = true };

	for (;;) {
		size_t length;
		int err;

		path += strspn(path, "/");
		length = strcspn(path, "/");
		if (length == 0) {
			return 0;
		}

		if (length > NAME_MAX_LENGTH || ++depth > PATH_DEPTH_MAX) {
			return -ENAMETOOLONG;
		}

		if (!(length == 1 && path[0] == '.')) {
			err = step_into(fat, node, path, length);
			if (err != 0) {
				return err;
			}
		}

		path += length;
	}
}

static int read_run(struct fat *fat, uint32_t first, uint32_t within, uint8_t *out, size_t length)
{
	return blockdev_read_bytes(fat->device, fat_cluster_offset(fat, first) + within, out, length);
}

int fat_read_data(struct fat *fat, const struct fat_node *node, uint64_t offset, void *buffer, size_t length,
		  size_t *done)
{
	uint8_t *out = buffer;
	uint32_t cluster = node->cluster;
	uint32_t steps = 0;
	uint64_t skip;
	int err = 0;

	*done = 0;
	if (offset >= node->size) {
		return 0;
	}

	if (length > node->size - offset) {
		length = (size_t)(node->size - offset);
	}

	for (skip = offset / fat->cluster_size; skip > 0 && err == 0; skip--) {
		err = ++steps > fat->cluster_count ? -EBADMSG : fat_next(fat, cluster, &cluster);
	}

	offset %= fat->cluster_size;

	while (err == 0 && length > 0) {
		uint64_t span = fat->cluster_size - offset;
		uint32_t following = 0;
		uint32_t last = cluster;
		size_t chunk;

		if (cluster == 0) {
			return -EBADMSG;
		}

		while (err == 0 && span < length) {
			err = ++steps > fat->cluster_count ? -EBADMSG : fat_next(fat, last, &following);
			if (err != 0 || following != last + 1) {
				break;
			}

			last = following;
			span += fat->cluster_size;
		}

		if (err != 0) {
			break;
		}

		chunk = span < length ? (size_t)span : length;
		err = read_run(fat, cluster, (uint32_t)offset, out, chunk);
		if (err == 0) {
			out += chunk;
			length -= chunk;
			*done += chunk;
			offset = 0;
			cluster = following;
		}
	}

	return err;
}
