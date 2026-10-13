#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>

#include "private.h"

#define EXTENT_MAGIC		0xf30a
#define EXTENT_HEADER		12
#define EXTENT_ENTRY		12
#define EXTENT_DEPTH_MAX	5
#define EXTENT_UNINITIALIZED	32768

#define HEADER_MAGIC		0
#define HEADER_ENTRIES		2
#define HEADER_MAXIMUM		4
#define HEADER_DEPTH		6
#define ENTRY_BLOCK		0
#define LEAF_LENGTH		4
#define LEAF_START_HIGH		6
#define LEAF_START_LOW		8
#define INDEX_CHILD_LOW		4
#define INDEX_CHILD_HIGH	8
#define DIRECT_BLOCKS		12
#define INDIRECT_LEVELS		3
#define RUN_BLOCKS_MAX		4096

struct mapping {
	uint64_t	physical;
	uint64_t	run;
};

static int check_extent_node(const uint8_t *node, size_t size, int expected_depth)
{
	uint16_t entries = get_le16(node + HEADER_ENTRIES);
	uint16_t maximum = get_le16(node + HEADER_MAXIMUM);
	uint16_t depth = get_le16(node + HEADER_DEPTH);

	if (get_le16(node + HEADER_MAGIC) != EXTENT_MAGIC || entries > maximum ||
	    EXTENT_HEADER + (size_t)maximum * EXTENT_ENTRY > size || depth > EXTENT_DEPTH_MAX ||
	    (expected_depth >= 0 && depth != expected_depth)) {
		return -EBADMSG;
	}

	return 0;
}

static int choose_entry(const uint8_t *node, uint64_t logical, const uint8_t **chosen)
{
	uint16_t entries = get_le16(node + HEADER_ENTRIES);
	uint16_t i;

	*chosen = NULL;

	for (i = 0; i < entries; i++) {
		const uint8_t *entry = node + EXTENT_HEADER + (size_t)i * EXTENT_ENTRY;

		if (i > 0 && get_le32(entry + ENTRY_BLOCK) <= get_le32(entry - EXTENT_ENTRY + ENTRY_BLOCK)) {
			return -EBADMSG;
		}

		if (get_le32(entry + ENTRY_BLOCK) > logical) {
			break;
		}

		*chosen = entry;
	}

	return 0;
}

static int map_leaf(const struct ext *ext, const uint8_t *entry, uint64_t logical, struct mapping *mapping)
{
	uint64_t first = get_le32(entry + ENTRY_BLOCK);
	uint64_t length = get_le16(entry + LEAF_LENGTH);
	uint64_t start = (uint64_t)get_le16(entry + LEAF_START_HIGH) << 32 | get_le32(entry + LEAF_START_LOW);
	bool initialized = length <= EXTENT_UNINITIALIZED;

	if (!initialized) {
		length -= EXTENT_UNINITIALIZED;
	}

	if (logical - first >= length) {
		return 0;
	}

	if (start == 0 || start >= ext->block_count || length > ext->block_count - start) {
		return -EBADMSG;
	}

	mapping->physical = initialized ? start + (logical - first) : 0;
	mapping->run = length - (logical - first);

	return 0;
}

static int map_extents(struct ext *ext, const struct ext_inode *inode, uint64_t logical, struct mapping *mapping,
		       uint8_t *scratch)
{
	const uint8_t *node = inode->block;
	size_t size = EXT_INLINE_SIZE;
	int expected = -1;

	if (logical > UINT32_MAX) {
		return 0;
	}

	for (;;) {
		const uint8_t *chosen;
		uint64_t child;
		int err = check_extent_node(node, size, expected);

		if (err == 0) {
			err = choose_entry(node, logical, &chosen);
		}

		if (err != 0 || chosen == NULL) {
			return err;
		}

		if (get_le16(node + HEADER_DEPTH) == 0) {
			return map_leaf(ext, chosen, logical, mapping);
		}

		child = (uint64_t)get_le16(chosen + INDEX_CHILD_HIGH) << 32 | get_le32(chosen + INDEX_CHILD_LOW);
		err = ext_read_block(ext, child, scratch);
		if (err != 0) {
			return err;
		}

		expected = get_le16(node + HEADER_DEPTH) - 1;
		node = scratch;
		size = ext->block_size;
	}
}

static int map_indirect(struct ext *ext, const struct ext_inode *inode, uint64_t logical, struct mapping *mapping,
			uint8_t *scratch)
{
	uint64_t per_block = ext->block_size / sizeof(uint32_t);
	uint64_t span = 1;
	uint64_t block;
	unsigned int level;

	if (logical < DIRECT_BLOCKS) {
		block = get_le32(inode->block + logical * sizeof(uint32_t));
	} else {
		logical -= DIRECT_BLOCKS;

		for (level = 1; level <= INDIRECT_LEVELS; level++) {
			span *= per_block;
			if (logical < span) {
				break;
			}
			logical -= span;
		}

		if (level > INDIRECT_LEVELS) {
			return 0;
		}

		block = get_le32(inode->block + (DIRECT_BLOCKS + level - 1) * sizeof(uint32_t));
		for (; level > 0 && block != 0; level--) {
			int err = ext_read_block(ext, block, scratch);

			if (err != 0) {
				return err;
			}

			span /= per_block;
			block = get_le32(scratch + (logical / span) * sizeof(uint32_t));
			logical %= span;
		}
	}

	if (block >= ext->block_count) {
		return -EBADMSG;
	}

	mapping->physical = block;

	return 0;
}

static int map_block(struct ext *ext, const struct ext_inode *inode, uint64_t logical, struct mapping *mapping,
		     uint8_t *scratch)
{
	mapping->physical = 0;
	mapping->run = 1;

	if ((inode->flags & EXT_FLAG_EXTENTS) != 0) {
		return map_extents(ext, inode, logical, mapping, scratch);
	}

	return map_indirect(ext, inode, logical, mapping, scratch);
}

static int read_inline(const struct ext_inode *inode, uint64_t offset, uint8_t *out, size_t length, size_t *done)
{
	if (inode->size > EXT_INLINE_SIZE) {
		return -ENOTSUP;
	}

	memcpy(out, inode->block + offset, length);
	*done = length;

	return 0;
}

static int read_blocks(struct ext *ext, const struct mapping *mapping, uint8_t *out, size_t count)
{
	if (mapping->physical == 0) {
		memset(out, 0, count * ext->block_size);
		return 0;
	}

	return blockdev_read_bytes(ext->device, mapping->physical * ext->block_size, out, count * ext->block_size);
}

int ext_read_data(struct ext *ext, const struct ext_inode *inode, uint64_t offset, void *buffer, size_t length,
		  size_t *done)
{
	uint8_t *out = buffer;
	uint8_t *scratch;
	uint8_t *bounce;
	int err = 0;

	*done = 0;
	if ((inode->flags & EXT_FLAG_ENCRYPTED) != 0) {
		return -EACCES;
	}

	if (offset >= inode->size) {
		return 0;
	}

	if (length > inode->size - offset) {
		length = (size_t)(inode->size - offset);
	}

	if ((inode->flags & EXT_FLAG_INLINE) != 0) {
		return read_inline(inode, offset, out, length, done);
	}

	scratch = malloc(ext->block_size);
	bounce = malloc(ext->block_size);
	if (scratch == NULL || bounce == NULL) {
		err = -ENOMEM;
	}

	while (err == 0 && length > 0) {
		size_t within = (size_t)(offset % ext->block_size);
		struct mapping mapping;
		size_t chunk;

		err = map_block(ext, inode, offset / ext->block_size, &mapping, scratch);
		if (err != 0) {
			break;
		}

		if (within != 0 || length < ext->block_size) {
			chunk = ext->block_size - within < length ? ext->block_size - within : length;
			err = read_blocks(ext, &mapping, bounce, 1);
			if (err == 0) {
				memcpy(out, bounce + within, chunk);
			}
		} else {
			uint64_t blocks = length / ext->block_size;

			if (blocks > mapping.run) {
				blocks = mapping.run;
			}

			if (blocks > RUN_BLOCKS_MAX) {
				blocks = RUN_BLOCKS_MAX;
			}

			chunk = (size_t)blocks * ext->block_size;
			err = read_blocks(ext, &mapping, out, (size_t)blocks);
		}

		if (err == 0) {
			out += chunk;
			offset += chunk;
			length -= chunk;
			*done += chunk;
		}
	}

	free(bounce);
	free(scratch);

	return err;
}
