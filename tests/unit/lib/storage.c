#include <errno.h>
#include <lib/blockdev.h>
#include <lib/cksum.h>
#include <lib/partition.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>

#include "harness.h"

#define SECTOR		512
#define FUZZ_ROUNDS	3000
#define FUZZ_SPAN	(64 * 1024)

static struct partition_table table;

struct disk {
	struct blockdev	device;
	uint8_t		*data;
	size_t		size;
};

static bool load_disk(const char *path, struct disk *disk)
{
	disk->data = test_load(path, &disk->size);

	return disk->data != NULL &&
	       blockdev_init_memory(&disk->device, "disk", disk->data, disk->size, SECTOR, false) == 0;
}

static bool blank_disk(struct disk *disk, size_t size)
{
	disk->size = size;
	disk->data = calloc(1, size);

	return disk->data != NULL &&
	       blockdev_init_memory(&disk->device, "disk", disk->data, size, SECTOR, false) == 0;
}

TEST(cksum, matches_reference_crc32)
{
	CHECK_EQ(crc32("123456789", 9), 0xcbf43926);
	CHECK_EQ(crc32_update(crc32("1234", 4), "56789", 5), 0xcbf43926);
	CHECK_EQ(crc32("", 0), 0);
}

TEST(blockdev, reads_bytes_across_blocks)
{
	struct disk disk;
	uint8_t buffer[3000];
	size_t i;

	CHECK(blank_disk(&disk, 8 * SECTOR));
	for (i = 0; i < disk.size; i++) {
		disk.data[i] = (uint8_t)(i * 7 + 3);
	}

	CHECK_EQ(blockdev_read_bytes(&disk.device, 100, buffer, sizeof(buffer)), 0);
	CHECK(memcmp(buffer, disk.data + 100, sizeof(buffer)) == 0);
	CHECK_EQ(blockdev_read_bytes(&disk.device, 1024, buffer, 512), 0);
	CHECK(memcmp(buffer, disk.data + 1024, 512) == 0);
	CHECK_EQ(blockdev_read_bytes(&disk.device, 4095, buffer, 1), 0);
	CHECK_EQ(buffer[0], disk.data[4095]);
	CHECK_EQ(blockdev_read_bytes(&disk.device, 4000, buffer, 97), -ERANGE);
	CHECK_EQ(blockdev_read_bytes(&disk.device, UINT64_MAX, buffer, 2), -ERANGE);
	CHECK_EQ(blockdev_read(&disk.device, 7, 2, buffer), -ERANGE);
	CHECK_EQ(blockdev_read(&disk.device, 0, 0, buffer), -ERANGE);
	free(disk.data);
}

TEST(blockdev, keeps_partitions_inside_their_disk)
{
	struct blockdev part;
	struct disk disk;
	uint8_t buffer[SECTOR];

	CHECK(blank_disk(&disk, 16 * SECTOR));
	disk.data[4 * SECTOR] = 0x42;
	CHECK_EQ(blockdev_init_partition(&part, &disk.device, 1, 4, 8), 0);
	CHECK_STR(part.name, "diskp1");
	CHECK_EQ(blockdev_read(&part, 0, 1, buffer), 0);
	CHECK_EQ(buffer[0], 0x42);
	CHECK_EQ(blockdev_read(&part, 7, 2, buffer), -ERANGE);
	CHECK_EQ(blockdev_init_partition(&part, &disk.device, 12, 10, 7), -ERANGE);
	CHECK_EQ(blockdev_init_partition(&part, &disk.device, 12, 10, 6), 0);
	CHECK_STR(part.name, "diskp12");
	disk.device.read_only = true;
	CHECK_EQ(blockdev_write(&disk.device, 0, 1, buffer), -EROFS);
	free(disk.data);
}

TEST(partition, finds_gpt_partitions)
{
	char guid[PARTITION_GUID_TEXT];
	struct disk disk;

	CHECK(load_disk(TEST_GPT, &disk));
	CHECK_EQ(partition_scan(&disk.device, &table), 0);
	CHECK_EQ(table.scheme, PARTITION_SCHEME_GPT);
	CHECK(!table.from_backup);
	CHECK_EQ(table.count, 3);
	CHECK_EQ(table.entries[0].index, 1);
	CHECK_EQ(table.entries[0].first, 2048);
	CHECK_EQ(table.entries[0].count, 8 * 2048);
	CHECK_STR(table.entries[0].label, "boot");
	CHECK_STR(table.entries[1].label, "system");
	CHECK_STR(table.entries[2].label, "data");
	partition_format_guid(table.entries[0].type, guid);
	CHECK_STR(guid, "c12a7328-f81f-11d2-ba4b-00a0c93ec93b");
	partition_format_guid(table.entries[1].type, guid);
	CHECK_STR(guid, "b0070001-2a2a-4c4f-9a3e-0123456789ab");
	free(disk.data);
}

TEST(partition, falls_back_to_backup_gpt)
{
	struct disk disk;

	CHECK(load_disk(TEST_GPT, &disk));
	disk.data[SECTOR + 30] ^= 0xff;
	CHECK_EQ(partition_scan(&disk.device, &table), 0);
	CHECK(table.from_backup);
	CHECK_EQ(table.count, 3);
	CHECK_STR(table.entries[2].label, "data");
	free(disk.data);
}

TEST(partition, rejects_gpt_with_damaged_entries)
{
	struct disk disk;

	CHECK(load_disk(TEST_GPT, &disk));
	disk.data[2 * SECTOR + 60] ^= 0x01;
	disk.data[disk.size - 33 * SECTOR + 60] ^= 0x01;
	CHECK_EQ(partition_scan(&disk.device, &table), -EBADMSG);
	CHECK_EQ(table.count, 0);
	free(disk.data);
}

TEST(partition, finds_mbr_with_logical_partitions)
{
	struct disk disk;

	CHECK(load_disk(TEST_MBR, &disk));
	CHECK_EQ(partition_scan(&disk.device, &table), 0);
	CHECK_EQ(table.scheme, PARTITION_SCHEME_MBR);
	CHECK_EQ(table.count, 4);
	CHECK_EQ(table.entries[0].index, 1);
	CHECK_EQ(table.entries[0].mbr_type, 0x0c);
	CHECK_EQ(table.entries[1].index, 2);
	CHECK_EQ(table.entries[2].index, 5);
	CHECK_EQ(table.entries[2].count, 4 * 2048);
	CHECK_EQ(table.entries[3].index, 6);
	CHECK_EQ(table.entries[3].mbr_type, 0x07);
	CHECK(table.entries[3].first > table.entries[2].first + table.entries[2].count - 1);
	free(disk.data);
}

TEST(partition, accepts_blank_disk)
{
	struct disk disk;

	CHECK(blank_disk(&disk, 1024 * SECTOR));
	CHECK_EQ(partition_scan(&disk.device, &table), 0);
	CHECK_EQ(table.scheme, PARTITION_SCHEME_NONE);
	CHECK_EQ(table.count, 0);
	free(disk.data);
}

static void write_mbr_entry(uint8_t *sector, unsigned int slot, uint8_t type, uint32_t first, uint32_t count)
{
	uint8_t *entry = sector + 446 + slot * 16;

	entry[4] = type;
	put_le32(entry + 8, first);
	put_le32(entry + 12, count);
	sector[510] = 0x55;
	sector[511] = 0xaa;
}

TEST(partition, rejects_broken_mbr_chains)
{
	struct disk disk;

	CHECK(blank_disk(&disk, 1024 * SECTOR));
	write_mbr_entry(disk.data, 0, 0x05, 100, 500);
	write_mbr_entry(disk.data + 100 * SECTOR, 0, 0x0c, 10, 20);
	write_mbr_entry(disk.data + 100 * SECTOR, 1, 0x05, 0, 50);
	CHECK_EQ(partition_scan(&disk.device, &table), 0);
	CHECK_EQ(table.count, 1);

	write_mbr_entry(disk.data + 100 * SECTOR, 1, 0x05, 0, 50);
	put_le32(disk.data + 100 * SECTOR + 446 + 16 + 8, 0);
	write_mbr_entry(disk.data, 1, 0x0c, 900, 200);
	CHECK_EQ(partition_scan(&disk.device, &table), -EBADMSG);

	memset(disk.data, 0, 2 * SECTOR);
	write_mbr_entry(disk.data, 0, 0x05, 100, 500);
	write_mbr_entry(disk.data + 100 * SECTOR, 0, 0x0c, 10, 20);
	write_mbr_entry(disk.data + 100 * SECTOR, 1, 0x05, 50, 50);
	write_mbr_entry(disk.data + 150 * SECTOR, 1, 0x05, 0, 50);
	put_le32(disk.data + 150 * SECTOR + 446 + 16 + 8, 50);
	CHECK_EQ(partition_scan(&disk.device, &table), -EBADMSG);
	free(disk.data);
}

static uint64_t random_state = 0x853c49e6748fea9bULL;

static void fix_gpt_checksums(uint8_t *data, size_t size, uint64_t lba)
{
	uint8_t *header = data + lba * SECTOR;
	uint32_t header_size = get_le32(header + 12);
	uint64_t entries = get_le64(header + 72);
	uint64_t entries_bytes = (uint64_t)get_le32(header + 80) * get_le32(header + 84);

	if (entries < size / SECTOR && entries_bytes <= size - entries * SECTOR && entries_bytes < 1024 * 1024) {
		put_le32(header + 88, crc32(data + entries * SECTOR, (size_t)entries_bytes));
	}

	if (header_size >= 92 && header_size <= SECTOR) {
		put_le32(header + 16, 0);
		put_le32(header + 16, crc32(header, header_size));
	}
}

static void restore(uint8_t *work, const uint8_t *original, size_t size)
{
	memcpy(work, original, FUZZ_SPAN);
	memcpy(work + size - FUZZ_SPAN, original + size - FUZZ_SPAN, FUZZ_SPAN);
}

static void mutate(uint8_t *data, size_t size)
{
	unsigned int count = 1 + test_random(&random_state) % 6;

	while (count-- > 0) {
		size_t at = test_random(&random_state) % FUZZ_SPAN;

		if (test_random(&random_state) % 2 != 0) {
			at = size - 1 - at;
		}

		data[at] = test_random(&random_state) % 4 == 0 ? 0xff : (uint8_t)test_random(&random_state);
	}

	if (test_random(&random_state) % 2 != 0) {
		fix_gpt_checksums(data, size, 1);
		fix_gpt_checksums(data, size, size / SECTOR - 1);
	}
}

static bool scan_is_sane(struct blockdev *disk)
{
	size_t i;

	if (partition_scan(disk, &table) != 0) {
		return table.count == 0;
	}

	for (i = 0; i < table.count; i++) {
		const struct partition *entry = &table.entries[i];

		if (entry->count == 0 || entry->first >= disk->block_count ||
		    entry->count > disk->block_count - entry->first) {
			return false;
		}
	}

	return true;
}

static void fuzz_disk(const char *path)
{
	struct disk original;
	struct disk work;
	unsigned int round;

	CHECK(load_disk(path, &original));
	CHECK(original.size >= 2 * FUZZ_SPAN);
	work.size = original.size;
	work.data = test_guarded(work.size);
	CHECK(work.data != NULL);
	CHECK_EQ(blockdev_init_memory(&work.device, "disk", work.data, work.size, SECTOR, false), 0);
	memcpy(work.data, original.data, work.size);

	for (round = 0; round < FUZZ_ROUNDS; round++) {
		restore(work.data, original.data, work.size);
		mutate(work.data, work.size);
		CHECK(scan_is_sane(&work.device));
	}

	test_release(work.data, work.size);
	free(original.data);
}

TEST(partition, survives_fuzzed_gpt)
{
	fuzz_disk(TEST_GPT);
}

TEST(partition, survives_fuzzed_mbr)
{
	fuzz_disk(TEST_MBR);
}
