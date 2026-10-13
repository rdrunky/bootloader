#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "private.h"

struct partition *partition_add(struct partition_table *table)
{
	struct partition *entry;

	if (table->count == PARTITIONS_MAX) {
		return NULL;
	}

	entry = &table->entries[table->count++];
	memset(entry, 0, sizeof(*entry));

	return entry;
}

int partition_scan(struct blockdev *disk, struct partition_table *table)
{
	bool protective = false;
	int err;

	memset(table, 0, sizeof(*table));
	err = mbr_scan(disk, table, &protective);
	if (err == 0 && !protective) {
		table->scheme = PARTITION_SCHEME_MBR;
		return 0;
	}

	memset(table, 0, sizeof(*table));
	if (err != 0 && err != -ENOENT) {
		return err;
	}

	err = gpt_scan(disk, table);
	if (err == 0) {
		table->scheme = PARTITION_SCHEME_GPT;
		return 0;
	}

	memset(table, 0, sizeof(*table));

	return protective || err != -ENOENT ? err : 0;
}

void partition_format_guid(const uint8_t *guid, char *text)
{
	snprintf(text, PARTITION_GUID_TEXT, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		 guid[3], guid[2], guid[1], guid[0], guid[5], guid[4], guid[7], guid[6],
		 guid[8], guid[9], guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
}
