#ifndef FS_PRIVATE_H
#define FS_PRIVATE_H

#include <lib/fs.h>
#include <stdbool.h>
#include <stdint.h>

struct fs_ops {
	int	(*stat)(struct fs *fs, const char *path, struct fs_stat *stat);
	int	(*read)(struct fs *fs, const char *path, uint64_t offset, void *buffer, size_t length, size_t *done);
	int	(*list)(struct fs *fs, const char *path, fs_visit visit, void *context);
	void	(*release)(struct fs *fs);
};

struct fs {
	const char		*type;
	char			label[FS_LABEL_MAX];
	struct blockdev		*device;
	const struct fs_ops	*ops;
	void			*state;
};

static inline bool is_power_of_two(uint32_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

int ext_mount(struct fs *fs);
int fat_mount(struct fs *fs);

#endif
