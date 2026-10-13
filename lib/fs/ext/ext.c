#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>

#include "private.h"

#define SUPERBLOCK_OFFSET	1024
#define SUPERBLOCK_SIZE		1024
#define EXT_MAGIC		0xef53
#define LOG_BLOCK_SIZE_MAX	6
#define GOOD_OLD_INODE_SIZE	128
#define INODE_BYTES		128

#define SB_INODES_COUNT		0x00
#define SB_BLOCKS_COUNT_LO	0x04
#define SB_FIRST_DATA_BLOCK	0x14
#define SB_LOG_BLOCK_SIZE	0x18
#define SB_BLOCKS_PER_GROUP	0x20
#define SB_INODES_PER_GROUP	0x28
#define SB_MAGIC		0x38
#define SB_REV_LEVEL		0x4c
#define SB_INODE_SIZE		0x58
#define SB_FEATURE_COMPAT	0x5c
#define SB_FEATURE_INCOMPAT	0x60
#define SB_FEATURE_RO_COMPAT	0x64
#define SB_VOLUME_NAME		0x78
#define SB_VOLUME_NAME_SIZE	16
#define SB_DESC_SIZE		0xfe
#define SB_FIRST_META_BG	0x104
#define SB_BLOCKS_COUNT_HI	0x150

#define GD_INODE_TABLE_LO	0x08
#define GD_INODE_TABLE_HI	0x28
#define GD_SIZE_MIN		32
#define GD_SIZE_64BIT		64

#define INODE_MODE		0x00
#define INODE_SIZE_LO		0x04
#define INODE_FLAGS		0x20
#define INODE_BLOCK		0x28
#define INODE_SIZE_HIGH		0x6c

#define COMPAT_HAS_JOURNAL	0x0004

#define INCOMPAT_RECOVER	0x00004
#define INCOMPAT_MMP		0x00100
#define INCOMPAT_FLEX_BG	0x00200
#define INCOMPAT_EA_INODE	0x00400
#define INCOMPAT_CSUM_SEED	0x02000
#define INCOMPAT_LARGEDIR	0x04000
#define INCOMPAT_INLINE_DATA	0x08000
#define INCOMPAT_ENCRYPT	0x10000
#define INCOMPAT_CASEFOLD	0x20000

#define INCOMPAT_SUPPORTED	(EXT_INCOMPAT_FILETYPE | INCOMPAT_RECOVER | EXT_INCOMPAT_META_BG | \
				 EXT_INCOMPAT_EXTENTS | EXT_INCOMPAT_64BIT | INCOMPAT_MMP | INCOMPAT_FLEX_BG | \
				 INCOMPAT_EA_INODE | INCOMPAT_CSUM_SEED | INCOMPAT_LARGEDIR | INCOMPAT_INLINE_DATA | \
				 INCOMPAT_ENCRYPT | INCOMPAT_CASEFOLD)

int ext_read_block(struct ext *ext, uint64_t block, void *buffer)
{
	if (block == 0 || block >= ext->block_count) {
		return -EBADMSG;
	}

	return blockdev_read_bytes(ext->device, block * ext->block_size, buffer, ext->block_size);
}

static void copy_label(char *label, const uint8_t *name)
{
	size_t i;

	for (i = 0; i < SB_VOLUME_NAME_SIZE && i + 1 < FS_LABEL_MAX && name[i] != '\0'; i++) {
		label[i] = isprint(name[i]) ? (char)name[i] : '?';
	}

	label[i] = '\0';
}

static int parse_superblock(struct ext *ext, const uint8_t *sb, struct fs *fs)
{
	uint32_t log_block_size = get_le32(sb + SB_LOG_BLOCK_SIZE);
	uint64_t device_blocks;
	uint64_t groups;

	if (log_block_size > LOG_BLOCK_SIZE_MAX) {
		return -EBADMSG;
	}

	ext->block_size = 1024u << log_block_size;
	ext->incompat = get_le32(sb + SB_FEATURE_INCOMPAT);
	ext->ro_compat = get_le32(sb + SB_FEATURE_RO_COMPAT);
	ext->block_count = get_le32(sb + SB_BLOCKS_COUNT_LO);
	if ((ext->incompat & EXT_INCOMPAT_64BIT) != 0) {
		ext->block_count |= (uint64_t)get_le32(sb + SB_BLOCKS_COUNT_HI) << 32;
	}

	if ((ext->incompat & ~INCOMPAT_SUPPORTED) != 0) {
		return -ENOTSUP;
	}

	ext->inode_count = get_le32(sb + SB_INODES_COUNT);
	ext->blocks_per_group = get_le32(sb + SB_BLOCKS_PER_GROUP);
	ext->inodes_per_group = get_le32(sb + SB_INODES_PER_GROUP);
	ext->first_data_block = get_le32(sb + SB_FIRST_DATA_BLOCK);
	ext->first_meta_bg = get_le32(sb + SB_FIRST_META_BG);
	ext->inode_size = get_le32(sb + SB_REV_LEVEL) == 0 ? GOOD_OLD_INODE_SIZE : get_le16(sb + SB_INODE_SIZE);
	ext->desc_size = (ext->incompat & EXT_INCOMPAT_64BIT) != 0 ? get_le16(sb + SB_DESC_SIZE) : GD_SIZE_MIN;

	if (ext->blocks_per_group == 0 || ext->blocks_per_group > ext->block_size * 8 ||
	    ext->inodes_per_group == 0 || ext->inodes_per_group > ext->block_size * 8 ||
	    ext->first_data_block >= ext->block_count || ext->first_data_block > 1) {
		return -EBADMSG;
	}

	if (ext->inode_size < GOOD_OLD_INODE_SIZE || !is_power_of_two(ext->inode_size) ||
	    ext->inode_size > ext->block_size || ext->desc_size < GD_SIZE_MIN || !is_power_of_two(ext->desc_size) ||
	    ext->desc_size > ext->block_size) {
		return -EBADMSG;
	}

	groups = (ext->block_count - ext->first_data_block + ext->blocks_per_group - 1) / ext->blocks_per_group;
	if (groups == 0 || groups > UINT32_MAX || ext->inode_count == 0 ||
	    ext->inode_count > groups * ext->inodes_per_group) {
		return -EBADMSG;
	}

	device_blocks = blockdev_size(ext->device) / ext->block_size;
	if (ext->block_count > device_blocks) {
		return -EBADMSG;
	}

	copy_label(fs->label, sb + SB_VOLUME_NAME);

	if ((ext->incompat & EXT_INCOMPAT_EXTENTS) != 0) {
		fs->type = "ext4";
	} else if ((get_le32(sb + SB_FEATURE_COMPAT) & COMPAT_HAS_JOURNAL) != 0) {
		fs->type = "ext3";
	} else {
		fs->type = "ext2";
	}

	return 0;
}

static bool group_has_super(const struct ext *ext, uint32_t group)
{
	uint32_t base;

	if ((ext->ro_compat & EXT_RO_SPARSE_SUPER) == 0 || group <= 1) {
		return true;
	}

	for (base = 3; base <= 7; base += 2) {
		uint64_t power = base;

		while (power < group) {
			power *= base;
		}

		if (power == group) {
			return true;
		}
	}

	return false;
}

static uint64_t descriptor_offset(const struct ext *ext, uint32_t group)
{
	uint32_t per_block = ext->block_size / ext->desc_size;
	uint32_t meta = group / per_block;
	uint64_t block;

	if ((ext->incompat & EXT_INCOMPAT_META_BG) != 0 && meta >= ext->first_meta_bg) {
		uint32_t first = meta * per_block;

		block = ext->first_data_block + (uint64_t)first * ext->blocks_per_group +
			(group_has_super(ext, first) ? 1 : 0);
	} else {
		block = (uint64_t)ext->first_data_block + 1 + meta;
	}

	return block * ext->block_size + (uint64_t)(group % per_block) * ext->desc_size;
}

int ext_read_inode(struct ext *ext, uint32_t number, struct ext_inode *inode)
{
	uint8_t descriptor[GD_SIZE_64BIT];
	uint8_t raw[INODE_BYTES];
	uint32_t group;
	uint32_t index;
	uint64_t table;
	int err;

	if (number == 0 || number > ext->inode_count) {
		return -EBADMSG;
	}

	group = (number - 1) / ext->inodes_per_group;
	index = (number - 1) % ext->inodes_per_group;

	err = blockdev_read_bytes(ext->device, descriptor_offset(ext, group), descriptor,
				  ext->desc_size < GD_SIZE_64BIT ? ext->desc_size : GD_SIZE_64BIT);
	if (err != 0) {
		return err;
	}

	table = get_le32(descriptor + GD_INODE_TABLE_LO);
	if (ext->desc_size >= GD_SIZE_64BIT) {
		table |= (uint64_t)get_le32(descriptor + GD_INODE_TABLE_HI) << 32;
	}

	if (table == 0 || table >= ext->block_count) {
		return -EBADMSG;
	}

	err = blockdev_read_bytes(ext->device, table * ext->block_size + (uint64_t)index * ext->inode_size, raw,
				  sizeof(raw));
	if (err != 0) {
		return err;
	}

	inode->number = number;
	inode->mode = get_le16(raw + INODE_MODE);
	inode->flags = get_le32(raw + INODE_FLAGS);
	inode->size = get_le32(raw + INODE_SIZE_LO) | (uint64_t)get_le32(raw + INODE_SIZE_HIGH) << 32;
	memcpy(inode->block, raw + INODE_BLOCK, EXT_INLINE_SIZE);

	return 0;
}

static enum fs_kind kind_of(const struct ext_inode *inode)
{
	switch (inode->mode & EXT_MODE_TYPE) {
	case EXT_MODE_FILE:
		return FS_KIND_FILE;
	case EXT_MODE_DIRECTORY:
		return FS_KIND_DIRECTORY;
	case EXT_MODE_SYMLINK:
		return FS_KIND_SYMLINK;
	default:
		return FS_KIND_OTHER;
	}
}

static int ext_stat(struct fs *fs, const char *path, struct fs_stat *stat)
{
	struct ext_inode inode;
	int err = ext_lookup(fs->state, path, true, &inode);

	if (err == 0) {
		stat->kind = kind_of(&inode);
		stat->size = inode.size;
	}

	return err;
}

static int ext_read(struct fs *fs, const char *path, uint64_t offset, void *buffer, size_t length, size_t *done)
{
	struct ext_inode inode;
	int err = ext_lookup(fs->state, path, true, &inode);

	if (err != 0) {
		return err;
	}

	if (kind_of(&inode) == FS_KIND_DIRECTORY) {
		return -EISDIR;
	}

	return ext_read_data(fs->state, &inode, offset, buffer, length, done);
}

struct listing {
	struct ext	*ext;
	fs_visit	visit;
	void		*context;
	struct fs_entry	entry;
};

static int list_entry(void *context, uint32_t number, const char *name, size_t length)
{
	struct listing *listing = context;
	struct ext_inode inode;
	int err;

	if ((length == 1 && name[0] == '.') || (length == 2 && name[0] == '.' && name[1] == '.')) {
		return 0;
	}

	err = ext_read_inode(listing->ext, number, &inode);
	if (err != 0) {
		return err;
	}

	memcpy(listing->entry.name, name, length);
	listing->entry.name[length] = '\0';
	listing->entry.kind = kind_of(&inode);
	listing->entry.size = inode.size;

	return listing->visit(listing->context, &listing->entry);
}

static int ext_list(struct fs *fs, const char *path, fs_visit visit, void *context)
{
	struct listing *listing = malloc(sizeof(*listing));
	struct ext_inode directory;
	int err;

	if (listing == NULL) {
		return -ENOMEM;
	}

	listing->ext = fs->state;
	listing->visit = visit;
	listing->context = context;

	err = ext_lookup(fs->state, path, true, &directory);
	if (err == 0) {
		err = ext_iterate(fs->state, &directory, list_entry, listing);
	}

	free(listing);

	return err;
}

static void ext_release(struct fs *fs)
{
	free(fs->state);
}

static const struct fs_ops ext_ops = {
	.stat		= ext_stat,
	.read		= ext_read,
	.list		= ext_list,
	.release	= ext_release,
};

int ext_mount(struct fs *fs)
{
	struct ext_inode root;
	uint8_t *sb = malloc(SUPERBLOCK_SIZE);
	struct ext *ext = calloc(1, sizeof(*ext));
	int err;

	if (sb == NULL || ext == NULL) {
		free(sb);
		free(ext);
		return -ENOMEM;
	}

	ext->device = fs->device;
	err = blockdev_read_bytes(fs->device, SUPERBLOCK_OFFSET, sb, SUPERBLOCK_SIZE);
	if (err == 0 && get_le16(sb + SB_MAGIC) != EXT_MAGIC) {
		err = -ENOENT;
	}

	if (err == -ERANGE) {
		err = -ENOENT;
	}

	if (err == 0) {
		err = parse_superblock(ext, sb, fs);
	}

	if (err == 0) {
		err = ext_read_inode(ext, EXT_ROOT_INODE, &root);
	}

	if (err == 0 && (root.mode & EXT_MODE_TYPE) != EXT_MODE_DIRECTORY) {
		err = -EBADMSG;
	}

	free(sb);

	if (err != 0) {
		free(ext);
		return err;
	}

	fs->state = ext;
	fs->ops = &ext_ops;

	return 0;
}
