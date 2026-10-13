#include <errno.h>
#include <sys/fuse.h>

static const struct fuse_ops *fuses;

void fuse_register(const struct fuse_ops *ops)
{
	fuses = ops;
}

int fuse_read(const char *name, void *buffer, size_t size, size_t *length)
{
	if (fuses == NULL) {
		return -ENODEV;
	}

	return fuses->read(name, buffer, size, length);
}
