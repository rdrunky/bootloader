#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/macros.h>

#include "private.h"

static int (*const drivers[])(struct fs *fs) = {
	ext_mount,
	fat_mount,
};

static int check_path(const char *path)
{
	if (path == NULL || path[0] != '/') {
		return -EINVAL;
	}

	return strnlen(path, FS_PATH_MAX) < FS_PATH_MAX ? 0 : -ENAMETOOLONG;
}

int fs_mount(struct blockdev *device, struct fs **result)
{
	struct fs *fs = calloc(1, sizeof(*fs));
	size_t i;
	int err = -ENOENT;

	if (fs == NULL) {
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(drivers) && err == -ENOENT; i++) {
		memset(fs, 0, sizeof(*fs));
		fs->device = device;
		err = drivers[i](fs);
	}

	if (err != 0) {
		free(fs);
		return err;
	}

	*result = fs;

	return 0;
}

void fs_unmount(struct fs *fs)
{
	if (fs != NULL) {
		fs->ops->release(fs);
		free(fs);
	}
}

const char *fs_type(const struct fs *fs)
{
	return fs->type;
}

const char *fs_label(const struct fs *fs)
{
	return fs->label;
}

int fs_stat(struct fs *fs, const char *path, struct fs_stat *stat)
{
	int err = check_path(path);

	return err != 0 ? err : fs->ops->stat(fs, path, stat);
}

int fs_read(struct fs *fs, const char *path, uint64_t offset, void *buffer, size_t length, size_t *done)
{
	int err = check_path(path);

	*done = 0;
	if (err != 0) {
		return err;
	}

	return length == 0 ? 0 : fs->ops->read(fs, path, offset, buffer, length, done);
}

int fs_list(struct fs *fs, const char *path, fs_visit visit, void *context)
{
	int err = check_path(path);

	return err != 0 ? err : fs->ops->list(fs, path, visit, context);
}
