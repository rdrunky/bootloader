#ifndef LIB_FS_H
#define LIB_FS_H

#include <lib/blockdev.h>
#include <stddef.h>
#include <stdint.h>

#define FS_NAME_MAX	255
#define FS_PATH_MAX	1024
#define FS_LABEL_MAX	32

enum fs_kind {
	FS_KIND_FILE,
	FS_KIND_DIRECTORY,
	FS_KIND_SYMLINK,
	FS_KIND_OTHER,
};

struct fs_stat {
	enum fs_kind	kind;
	uint64_t	size;
};

struct fs_entry {
	char		name[FS_NAME_MAX + 1];
	enum fs_kind	kind;
	uint64_t	size;
};

typedef int (*fs_visit)(void *context, const struct fs_entry *entry);

struct fs;

int fs_mount(struct blockdev *device, struct fs **fs);
void fs_unmount(struct fs *fs);
const char *fs_type(const struct fs *fs);
const char *fs_label(const struct fs *fs);

int fs_stat(struct fs *fs, const char *path, struct fs_stat *stat);
int fs_read(struct fs *fs, const char *path, uint64_t offset, void *buffer, size_t length, size_t *done);
int fs_list(struct fs *fs, const char *path, fs_visit visit, void *context);

#endif
