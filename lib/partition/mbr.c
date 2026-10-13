#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>

#include "private.h"

#define MBR_SIZE		512
#define MBR_TABLE		446
#define MBR_ENTRY_SIZE		16
#define MBR_ENTRIES		4
#define MBR_SIGNATURE		510
#define MBR_LOGICAL_FIRST	5
#define MBR_LOGICAL_MAX		64

#define ENTRY_STATUS		0
#define ENTRY_TYPE		4
#define ENTRY_FIRST		8
#define ENTRY_COUNT		12

#define STATUS_ACTIVE		0x80
#define TYPE_PROTECTIVE		0xee

struct extent {
	uint64_t	first;
	uint64_t	count;
};

static bool is_extended(uint8_t type)
{
	return type == 0x05 || type == 0x0f || type == 0x85;
}

static int read_sector(struct blockdev *disk, uint64_t block, uint8_t *sector)
{
	int err;

	if (disk->block_size < MBR_SIZE) {
		return -ENOTSUP;
	}

	err = blockdev_read(disk, block, 1, sector);
	if (err != 0) {
		return err;
	}

	return sector[MBR_SIGNATURE] == 0x55 && sector[MBR_SIGNATURE + 1] == 0xaa ? 0 : -ENOENT;
}

static const uint8_t *entry_at(const uint8_t *sector, unsigned int index)
{
	return sector + MBR_TABLE + index * MBR_ENTRY_SIZE;
}

static bool statuses_valid(const uint8_t *sector)
{
	unsigned int i;

	for (i = 0; i < MBR_ENTRIES; i++) {
		uint8_t status = entry_at(sector, i)[ENTRY_STATUS];

		if (status != 0 && status != STATUS_ACTIVE) {
			return false;
		}
	}

	return true;
}

static bool extent_within(const struct extent *inner, const struct extent *outer)
{
	return inner->count != 0 && inner->first >= outer->first &&
	       inner->first - outer->first <= outer->count &&
	       inner->count <= outer->count - (inner->first - outer->first);
}

static int add_entry(struct partition_table *table, unsigned int index, uint8_t type, const struct extent *extent)
{
	struct partition *partition = partition_add(table);

	if (partition == NULL) {
		return -ENOSPC;
	}

	partition->index = index;
	partition->first = extent->first;
	partition->count = extent->count;
	partition->mbr_type = type;

	return 0;
}

static int scan_logical(struct blockdev *disk, struct partition_table *table, uint8_t *sector,
			const struct extent *extended)
{
	uint64_t ebr = extended->first;
	unsigned int index = MBR_LOGICAL_FIRST;

	while (index < MBR_LOGICAL_FIRST + MBR_LOGICAL_MAX) {
		const uint8_t *logical;
		const uint8_t *link;
		struct extent extent;
		uint64_t next;
		int err = read_sector(disk, ebr, sector);

		if (err != 0) {
			return err == -ENOENT ? -EBADMSG : err;
		}

		logical = entry_at(sector, 0);
		link = entry_at(sector, 1);

		if (logical[ENTRY_TYPE] != 0) {
			extent.first = ebr + get_le32(logical + ENTRY_FIRST);
			extent.count = get_le32(logical + ENTRY_COUNT);
			if (!extent_within(&extent, extended)) {
				return -EBADMSG;
			}

			err = add_entry(table, index++, logical[ENTRY_TYPE], &extent);
			if (err != 0) {
				return err;
			}
		}

		if (!is_extended(link[ENTRY_TYPE]) || get_le32(link + ENTRY_FIRST) == 0) {
			return 0;
		}

		next = extended->first + get_le32(link + ENTRY_FIRST);
		if (next <= ebr || next - extended->first >= extended->count) {
			return -EBADMSG;
		}

		ebr = next;
	}

	return -EBADMSG;
}

int mbr_scan(struct blockdev *disk, struct partition_table *table, bool *protective)
{
	struct extent whole = { 0, disk->block_count };
	uint8_t primary[MBR_ENTRIES * MBR_ENTRY_SIZE];
	uint8_t *sector = malloc(disk->block_size);
	unsigned int i;
	int err;

	if (sector == NULL) {
		return -ENOMEM;
	}

	err = read_sector(disk, 0, sector);
	if (err == 0 && !statuses_valid(sector)) {
		err = -ENOENT;
	}

	if (err == 0) {
		memcpy(primary, entry_at(sector, 0), sizeof(primary));
	}

	for (i = 0; err == 0 && i < MBR_ENTRIES; i++) {
		if (primary[i * MBR_ENTRY_SIZE + ENTRY_TYPE] == TYPE_PROTECTIVE) {
			*protective = true;
		}
	}

	for (i = 0; err == 0 && !*protective && i < MBR_ENTRIES; i++) {
		const uint8_t *entry = primary + i * MBR_ENTRY_SIZE;
		struct extent extent;

		if (entry[ENTRY_TYPE] == 0) {
			continue;
		}

		extent.first = get_le32(entry + ENTRY_FIRST);
		extent.count = get_le32(entry + ENTRY_COUNT);
		if (extent.first == 0 || !extent_within(&extent, &whole)) {
			err = -EBADMSG;
		} else if (is_extended(entry[ENTRY_TYPE])) {
			err = scan_logical(disk, table, sector, &extent);
		} else {
			err = add_entry(table, i + 1, entry[ENTRY_TYPE], &extent);
		}
	}

	free(sector);

	return err;
}
