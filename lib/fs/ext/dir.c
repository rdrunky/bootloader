#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>

#include "private.h"

#define DIRENT_INODE		0
#define DIRENT_RECORD		4
#define DIRENT_NAME_LENGTH	6
#define DIRENT_HEADER		8
#define DIRENT_MIN		12
#define DIRENT_ALIGN		4
#define DIRENT_NAME_MAX		255
#define BLOCK_SIZE_LARGE	65536
#define RECORD_LARGE		0xffff
#define INLINE_PARENT		4
#define SYMLINK_DEPTH		8
#define DIRECTORY_SIZE_MAX	(256ULL << 20)

struct search {
	const char	*name;
	size_t		length;
	uint32_t	found;
};

static int parse_block(const struct ext *ext, const uint8_t *block, size_t size, ext_visit visit, void *context)
{
	size_t offset = 0;

	while (offset + DIRENT_HEADER <= size) {
		const uint8_t *entry = block + offset;
		uint32_t inode = get_le32(entry + DIRENT_INODE);
		size_t record = get_le16(entry + DIRENT_RECORD);
		size_t length = (ext->incompat & EXT_INCOMPAT_FILETYPE) != 0 ? entry[DIRENT_NAME_LENGTH] :
									       get_le16(entry + DIRENT_NAME_LENGTH);

		if ((record == 0 || record == RECORD_LARGE) && size == BLOCK_SIZE_LARGE) {
			record = BLOCK_SIZE_LARGE;
		}

		if (record < DIRENT_MIN || record % DIRENT_ALIGN != 0 || record > size - offset ||
		    length > record - DIRENT_HEADER || length > DIRENT_NAME_MAX) {
			return -EBADMSG;
		}

		if (inode != 0 && length != 0) {
			int stop = visit(context, inode, (const char *)entry + DIRENT_HEADER, length);

			if (stop != 0) {
				return stop;
			}
		}

		offset += record;
	}

	return 0;
}

static int iterate_inline(struct ext *ext, const struct ext_inode *directory, ext_visit visit, void *context)
{
	int stop;

	if (directory->size > EXT_INLINE_SIZE) {
		return -ENOTSUP;
	}

	stop = visit(context, directory->number, ".", 1);

	if (stop == 0) {
		stop = visit(context, get_le32(directory->block), "..", 2);
	}

	if (stop == 0) {
		stop = parse_block(ext, directory->block + INLINE_PARENT, EXT_INLINE_SIZE - INLINE_PARENT, visit, context);
	}

	return stop;
}

int ext_iterate(struct ext *ext, const struct ext_inode *directory, ext_visit visit, void *context)
{
	uint8_t *block;
	uint64_t offset;
	int result = 0;

	if ((directory->mode & EXT_MODE_TYPE) != EXT_MODE_DIRECTORY) {
		return -ENOTDIR;
	}

	if ((directory->flags & EXT_FLAG_INLINE) != 0) {
		return iterate_inline(ext, directory, visit, context);
	}

	if (directory->size > DIRECTORY_SIZE_MAX) {
		return -EBADMSG;
	}

	block = malloc(ext->block_size);
	if (block == NULL) {
		return -ENOMEM;
	}

	for (offset = 0; offset < directory->size && result == 0; offset += ext->block_size) {
		size_t done;

		result = ext_read_data(ext, directory, offset, block, ext->block_size, &done);
		if (result == 0) {
			result = parse_block(ext, block, done, visit, context);
		}
	}

	free(block);

	return result;
}

static int match_name(void *context, uint32_t inode, const char *name, size_t length)
{
	struct search *search = context;

	if (length != search->length || memcmp(name, search->name, length) != 0) {
		return 0;
	}

	search->found = inode;

	return 1;
}

static int find_entry(struct ext *ext, const struct ext_inode *directory, const char *name, size_t length,
		      struct ext_inode *inode)
{
	struct search search = { name, length, 0 };
	int result = ext_iterate(ext, directory, match_name, &search);

	if (result < 0) {
		return result;
	}

	if (search.found == 0) {
		return -ENOENT;
	}

	return ext_read_inode(ext, search.found, inode);
}

static int read_link(struct ext *ext, const struct ext_inode *link, char *target)
{
	size_t done;
	int err;

	if (link->size == 0 || link->size >= FS_PATH_MAX) {
		return -EBADMSG;
	}

	if (link->size < EXT_INLINE_SIZE && (link->flags & (EXT_FLAG_EXTENTS | EXT_FLAG_INLINE)) == 0) {
		memcpy(target, link->block, (size_t)link->size);
	} else {
		err = ext_read_data(ext, link, 0, target, (size_t)link->size, &done);
		if (err != 0) {
			return err;
		}

		if (done != link->size) {
			return -EIO;
		}
	}

	target[link->size] = '\0';

	return 0;
}

static bool is_symlink(const struct ext_inode *inode)
{
	return (inode->mode & EXT_MODE_TYPE) == EXT_MODE_SYMLINK;
}

static int follow_link(struct ext *ext, const struct ext_inode *link, const char *rest, char *pending, char *target,
		       struct ext_inode *current)
{
	int err = read_link(ext, link, target);

	if (err != 0) {
		return err;
	}

	if (strlcat(target, rest, FS_PATH_MAX) >= FS_PATH_MAX) {
		return -ENAMETOOLONG;
	}

	strlcpy(pending, target, FS_PATH_MAX);

	return pending[0] == '/' ? ext_read_inode(ext, EXT_ROOT_INODE, current) : 0;
}

static int walk(struct ext *ext, char *pending, char *target, bool follow, struct ext_inode *current)
{
	const char *cursor = pending;
	unsigned int links = 0;
	int err = ext_read_inode(ext, EXT_ROOT_INODE, current);

	while (err == 0) {
		struct ext_inode next;
		const char *rest;
		size_t length;

		while (*cursor == '/') {
			cursor++;
		}

		if (*cursor == '\0') {
			break;
		}

		length = strcspn(cursor, "/");
		if (length > DIRENT_NAME_MAX) {
			return -ENAMETOOLONG;
		}

		if ((current->mode & EXT_MODE_TYPE) != EXT_MODE_DIRECTORY) {
			return -ENOTDIR;
		}

		rest = cursor + length;
		if (length == 1 && cursor[0] == '.') {
			cursor = rest;
			continue;
		}

		err = find_entry(ext, current, cursor, length, &next);
		if (err != 0) {
			break;
		}

		if (is_symlink(&next) && (follow || rest[strspn(rest, "/")] != '\0')) {
			if (++links > SYMLINK_DEPTH) {
				return -ELOOP;
			}

			err = follow_link(ext, &next, rest, pending, target, current);
			cursor = pending;
			continue;
		}

		*current = next;
		cursor = rest;
	}

	return err;
}

int ext_lookup(struct ext *ext, const char *path, bool follow, struct ext_inode *inode)
{
	char *pending = malloc(FS_PATH_MAX);
	char *target = malloc(FS_PATH_MAX);
	int err = -ENOMEM;

	if (pending != NULL && target != NULL) {
		strlcpy(pending, path, FS_PATH_MAX);
		err = walk(ext, pending, target, follow, inode);
	}

	free(target);
	free(pending);

	return err;
}
