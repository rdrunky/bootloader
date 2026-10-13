#ifndef EXT_PRIVATE_H
#define EXT_PRIVATE_H

#include <stdbool.h>

#include "../private.h"

#define EXT_ROOT_INODE		2
#define EXT_INLINE_SIZE		60

#define EXT_MODE_TYPE		0xf000
#define EXT_MODE_FILE		0x8000
#define EXT_MODE_DIRECTORY	0x4000
#define EXT_MODE_SYMLINK	0xa000

#define EXT_FLAG_ENCRYPTED	0x00000800
#define EXT_FLAG_EXTENTS	0x00080000
#define EXT_FLAG_INLINE		0x10000000

#define EXT_INCOMPAT_FILETYPE	0x0002
#define EXT_INCOMPAT_META_BG	0x0010
#define EXT_INCOMPAT_EXTENTS	0x0040
#define EXT_INCOMPAT_64BIT	0x0080
#define EXT_RO_SPARSE_SUPER	0x0001

struct ext_inode {
	uint32_t	number;
	uint16_t	mode;
	uint32_t	flags;
	uint64_t	size;
	uint8_t		block[EXT_INLINE_SIZE];
};

struct ext {
	struct blockdev	*device;
	uint32_t	block_size;
	uint64_t	block_count;
	uint32_t	inode_count;
	uint32_t	inodes_per_group;
	uint32_t	blocks_per_group;
	uint32_t	first_data_block;
	uint32_t	first_meta_bg;
	uint16_t	inode_size;
	uint16_t	desc_size;
	uint32_t	incompat;
	uint32_t	ro_compat;
};

typedef int (*ext_visit)(void *context, uint32_t inode, const char *name, size_t length);

int ext_read_block(struct ext *ext, uint64_t block, void *buffer);
int ext_read_inode(struct ext *ext, uint32_t number, struct ext_inode *inode);
int ext_read_data(struct ext *ext, const struct ext_inode *inode, uint64_t offset, void *buffer, size_t length,
		  size_t *done);
int ext_iterate(struct ext *ext, const struct ext_inode *directory, ext_visit visit, void *context);
int ext_lookup(struct ext *ext, const char *path, bool follow, struct ext_inode *inode);

#endif
