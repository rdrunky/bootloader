#ifndef SYS_FUSE_H
#define SYS_FUSE_H

#include <stddef.h>

struct fuse_ops {
	int	(*read)(const char *name, void *buffer, size_t size, size_t *length);
};

void fuse_register(const struct fuse_ops *ops);
int fuse_read(const char *name, void *buffer, size_t size, size_t *length);

#endif
