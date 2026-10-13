#include <errno.h>
#include <lib/partition.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/format.h>
#include <sys/storage.h>

static void attach_partition(struct blockdev *disk, const struct partition *entry)
{
	struct blockdev *part = malloc(sizeof(*part));

	if (part == NULL || blockdev_init_partition(part, disk, entry->index, entry->first, entry->count) != 0) {
		printf("%s partition %u unusable\n", disk->name, entry->index);
		free(part);
		return;
	}

	memcpy(part->label, entry->label, sizeof(part->label));
	part->mbr_type = entry->mbr_type;
	memcpy(part->type, entry->type, sizeof(part->type));
	memcpy(part->uuid, entry->uuid, sizeof(part->uuid));
	blockdev_register(part);
}

void storage_attach(struct blockdev *disk)
{
	struct partition_table *table = malloc(sizeof(*table));
	char size[SIZE_TEXT];
	size_t i;
	int err;

	blockdev_register(disk);
	format_size(size, sizeof(size), blockdev_size(disk));
	printf("disk %s %s ok\n", disk->name, size);

	if (table == NULL) {
		printf("%s partition table %s\n", disk->name, strerror(ENOMEM));
		return;
	}

	err = partition_scan(disk, table);
	if (err != 0) {
		printf("%s partition table %s\n", disk->name, strerror(-err));
	} else if (table->from_backup) {
		printf("%s primary gpt damaged, backup used\n", disk->name);
	}

	for (i = 0; err == 0 && i < table->count; i++) {
		attach_partition(disk, &table->entries[i]);
	}

	free(table);
}
