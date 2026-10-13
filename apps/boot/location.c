#include <errno.h>
#include <string.h>

#include "private.h"

int location_open(const char *spec, struct location *location)
{
	const char *colon = strchr(spec, ':');
	char name[BLOCKDEV_NAME_MAX];
	size_t length;

	if (colon == NULL) {
		return -EINVAL;
	}

	length = (size_t)(colon - spec);
	if (length == 0 || length >= sizeof(name)) {
		return -EINVAL;
	}

	memcpy(name, spec, length);
	name[length] = '\0';

	location->device = blockdev_find(name);
	if (location->device == NULL) {
		location->device = layout_resolve(name);
	}

	if (location->device == NULL) {
		return -ENODEV;
	}

	location->path = colon[1] != '\0' ? colon + 1 : "/";
	if (location->path[0] != '/') {
		return -EINVAL;
	}

	return fs_mount(location->device, &location->fs);
}

void location_close(struct location *location)
{
	fs_unmount(location->fs);
	location->fs = NULL;
}
