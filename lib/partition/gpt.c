#include <errno.h>
#include <lib/cksum.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>

#include "private.h"

#define GPT_SIGNATURE		"EFI PART"
#define GPT_HEADER_MIN		92
#define GPT_ENTRY_MIN		128
#define GPT_ENTRIES_MAX		1024
#define GPT_NAME_UNITS		36

#define HEADER_SIZE		12
#define HEADER_CRC		16
#define HEADER_MY_LBA		24
#define HEADER_FIRST_USABLE	40
#define HEADER_LAST_USABLE	48
#define HEADER_ENTRIES_LBA	72
#define HEADER_ENTRY_COUNT	80
#define HEADER_ENTRY_SIZE	84
#define HEADER_ENTRIES_CRC	88

#define ENTRY_TYPE		0
#define ENTRY_UUID		16
#define ENTRY_FIRST		32
#define ENTRY_LAST		40
#define ENTRY_NAME		56

struct gpt_header {
	uint64_t	first_usable;
	uint64_t	last_usable;
	uint64_t	entries_lba;
	uint32_t	entry_count;
	uint32_t	entry_size;
	uint32_t	entries_crc;
};

static bool guid_empty(const uint8_t *guid)
{
	unsigned int i;

	for (i = 0; i < BLOCKDEV_GUID_SIZE; i++) {
		if (guid[i] != 0) {
			return false;
		}
	}

	return true;
}

static int parse_header(struct blockdev *disk, uint8_t *block, uint64_t lba, struct gpt_header *header)
{
	uint32_t size = get_le32(block + HEADER_SIZE);
	uint32_t crc = get_le32(block + HEADER_CRC);
	uint64_t table_bytes;
	uint64_t table_blocks;

	if (memcmp(block, GPT_SIGNATURE, 8) != 0) {
		return -ENOENT;
	}

	if (size < GPT_HEADER_MIN || size > disk->block_size) {
		return -EBADMSG;
	}

	put_le32(block + HEADER_CRC, 0);
	if (crc32(block, size) != crc || get_le64(block + HEADER_MY_LBA) != lba) {
		return -EBADMSG;
	}

	header->first_usable = get_le64(block + HEADER_FIRST_USABLE);
	header->last_usable = get_le64(block + HEADER_LAST_USABLE);
	header->entries_lba = get_le64(block + HEADER_ENTRIES_LBA);
	header->entry_count = get_le32(block + HEADER_ENTRY_COUNT);
	header->entry_size = get_le32(block + HEADER_ENTRY_SIZE);
	header->entries_crc = get_le32(block + HEADER_ENTRIES_CRC);

	if (header->entry_size < GPT_ENTRY_MIN || header->entry_size % 8 != 0 ||
	    header->entry_count == 0 || header->entry_count > GPT_ENTRIES_MAX) {
		return -EBADMSG;
	}

	if (header->first_usable > header->last_usable || header->last_usable >= disk->block_count) {
		return -EBADMSG;
	}

	table_bytes = (uint64_t)header->entry_count * header->entry_size;
	table_blocks = (table_bytes + disk->block_size - 1) / disk->block_size;
	if (header->entries_lba == 0 || header->entries_lba >= disk->block_count ||
	    table_blocks > disk->block_count - header->entries_lba) {
		return -EBADMSG;
	}

	return 0;
}

static void copy_name(char *label, const uint8_t *name)
{
	unsigned int i;

	for (i = 0; i < GPT_NAME_UNITS && i + 1 < BLOCKDEV_LABEL_MAX; i++) {
		uint16_t unit = get_le16(name + i * 2);

		if (unit == 0) {
			break;
		}

		label[i] = unit >= 0x20 && unit < 0x7f ? (char)unit : '?';
	}

	label[i] = '\0';
}

static int parse_entries(const struct gpt_header *header, const uint8_t *entries, struct partition_table *table)
{
	uint32_t i;

	for (i = 0; i < header->entry_count; i++) {
		const uint8_t *entry = entries + (size_t)i * header->entry_size;
		uint64_t first = get_le64(entry + ENTRY_FIRST);
		uint64_t last = get_le64(entry + ENTRY_LAST);
		struct partition *partition;

		if (guid_empty(entry + ENTRY_TYPE)) {
			continue;
		}

		if (first > last || first < header->first_usable || last > header->last_usable) {
			return -EBADMSG;
		}

		partition = partition_add(table);
		if (partition == NULL) {
			return -ENOSPC;
		}

		partition->index = i + 1;
		partition->first = first;
		partition->count = last - first + 1;
		memcpy(partition->type, entry + ENTRY_TYPE, BLOCKDEV_GUID_SIZE);
		memcpy(partition->uuid, entry + ENTRY_UUID, BLOCKDEV_GUID_SIZE);
		copy_name(partition->label, entry + ENTRY_NAME);
	}

	return 0;
}

static int scan_at(struct blockdev *disk, uint64_t lba, struct partition_table *table)
{
	struct gpt_header header;
	uint8_t *block = malloc(disk->block_size);
	uint8_t *entries = NULL;
	size_t table_bytes;
	int err;

	if (block == NULL) {
		return -ENOMEM;
	}

	err = blockdev_read(disk, lba, 1, block);
	if (err == 0) {
		err = parse_header(disk, block, lba, &header);
	}

	if (err == 0) {
		table_bytes = (size_t)header.entry_count * header.entry_size;
		entries = malloc(table_bytes);
		err = entries != NULL ? blockdev_read_bytes(disk, header.entries_lba * disk->block_size, entries,
							    table_bytes) : -ENOMEM;
	}

	if (err == 0 && crc32(entries, table_bytes) != header.entries_crc) {
		err = -EBADMSG;
	}

	if (err == 0) {
		err = parse_entries(&header, entries, table);
	}

	free(entries);
	free(block);

	return err;
}

int gpt_scan(struct blockdev *disk, struct partition_table *table)
{
	int err;

	if (disk->block_count < 3) {
		return -ENOENT;
	}

	err = scan_at(disk, 1, table);
	if (err == 0) {
		return 0;
	}

	table->count = 0;
	if (scan_at(disk, disk->block_count - 1, table) == 0) {
		table->from_backup = true;
		return 0;
	}

	return err;
}
