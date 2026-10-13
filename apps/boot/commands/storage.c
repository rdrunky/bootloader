#include <lib/blockdev.h>
#include <lib/fs.h>
#include <lib/partition.h>
#include <stdio.h>
#include <sys/format.h>
#include <sys/shell.h>

#include "../private.h"

static void print_filesystem(struct blockdev *dev)
{
	struct fs *fs;

	if (fs_mount(dev, &fs) != 0) {
		return;
	}

	printf("  %s", fs_type(fs));
	if (fs_label(fs)[0] != '\0') {
		printf(" '%s'", fs_label(fs));
	}

	fs_unmount(fs);
}

COMMAND(disks, "", "list disks, partitions and filesystems")
{
	struct blockdev *dev;

	if (argc != 1) {
		return SHELL_USAGE;
	}

	for (dev = blockdev_list(); dev != NULL; dev = dev->next) {
		char size[SIZE_TEXT];
		char type[PARTITION_GUID_TEXT];

		format_size(size, sizeof(size), blockdev_size(dev));
		printf("  %-12s %10s%s", dev->name, size, dev->read_only ? "  read-only" : "");

		if (dev->parent != NULL && dev->mbr_type != 0) {
			printf("  %-12s mbr %#04x", "-", dev->mbr_type);
		} else if (dev->parent != NULL) {
			partition_format_guid(dev->type, type);
			printf("  %-12s %s", dev->label[0] != '\0' ? dev->label : "-", type);
		}

		print_filesystem(dev);
		printf("\n");
	}

	return 0;
}
