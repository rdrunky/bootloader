#ifndef LIB_PARTITION_H
#define LIB_PARTITION_H

#include <lib/blockdev.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PARTITIONS_MAX		128
#define PARTITION_GUID_TEXT	37

enum partition_scheme {
	PARTITION_SCHEME_NONE,
	PARTITION_SCHEME_MBR,
	PARTITION_SCHEME_GPT,
};

struct partition {
	unsigned int	index;
	uint64_t	first;
	uint64_t	count;
	uint8_t		mbr_type;
	uint8_t		type[BLOCKDEV_GUID_SIZE];
	uint8_t		uuid[BLOCKDEV_GUID_SIZE];
	char		label[BLOCKDEV_LABEL_MAX];
};

struct partition_table {
	enum partition_scheme	scheme;
	bool			from_backup;
	size_t			count;
	struct partition	entries[PARTITIONS_MAX];
};

int partition_scan(struct blockdev *disk, struct partition_table *table);
void partition_format_guid(const uint8_t *guid, char *text);

#endif
