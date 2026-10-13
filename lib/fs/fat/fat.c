#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>

#include "private.h"

#define BPB_BYTES_PER_SECTOR	0x0b
#define BPB_SECTORS_PER_CLUSTER	0x0d
#define BPB_RESERVED		0x0e
#define BPB_FAT_COUNT		0x10
#define BPB_ROOT_ENTRIES	0x11
#define BPB_TOTAL16		0x13
#define BPB_MEDIA		0x15
#define BPB_FAT_SIZE16		0x16
#define BPB_TOTAL32		0x20
#define BPB_FAT_SIZE32		0x24
#define BPB_VERSION32		0x2a
#define BPB_ROOT_CLUSTER	0x2c
#define BPB_LABEL16		0x2b
#define BPB_LABEL32		0x47
#define BPB_SIGNATURE		0x1fe

#define BOOT_SECTOR_SIZE	512
#define SIGNATURE_LOW		0x55
#define SIGNATURE_HIGH		0xaa
#define JUMP_SHORT		0xeb
#define JUMP_NEAR		0xe9
#define MEDIA_REMOVABLE		0xf0
#define MEDIA_FIXED_MIN		0xf8
#define SECTOR_SIZE_MIN		512
#define SECTOR_SIZE_MAX		4096
#define CLUSTER_SIZE_MAX	(256 * 1024)
#define FAT_COPIES_MAX		4
#define LABEL_SIZE		11
#define FAT12_CLUSTERS_MAX	4085
#define FAT16_CLUSTERS_MAX	65525
#define FAT32_CLUSTERS_MAX	0x0ffffff5

#define FAT12_END		0xff8
#define FAT16_END		0xfff8
#define FAT32_END		0x0ffffff8
#define FAT32_MASK		0x0fffffff

struct geometry {
	uint32_t	sector_size;
	uint32_t	per_cluster;
	uint32_t	reserved;
	uint32_t	copies;
	uint32_t	root_entries;
	uint64_t	total;
	uint64_t	fat_size;
};

static void read_geometry(const uint8_t *sector, struct geometry *geometry)
{
	geometry->sector_size = get_le16(sector + BPB_BYTES_PER_SECTOR);
	geometry->per_cluster = sector[BPB_SECTORS_PER_CLUSTER];
	geometry->reserved = get_le16(sector + BPB_RESERVED);
	geometry->copies = sector[BPB_FAT_COUNT];
	geometry->root_entries = get_le16(sector + BPB_ROOT_ENTRIES);
	geometry->total = get_le16(sector + BPB_TOTAL16);
	if (geometry->total == 0) {
		geometry->total = get_le32(sector + BPB_TOTAL32);
	}

	geometry->fat_size = get_le16(sector + BPB_FAT_SIZE16);
	if (geometry->fat_size == 0) {
		geometry->fat_size = get_le32(sector + BPB_FAT_SIZE32);
	}
}

static bool geometry_plausible(const uint8_t *sector, const struct geometry *geometry)
{
	uint8_t media = sector[BPB_MEDIA];

	return sector[BPB_SIGNATURE] == SIGNATURE_LOW && sector[BPB_SIGNATURE + 1] == SIGNATURE_HIGH &&
	       (sector[0] == JUMP_SHORT || sector[0] == JUMP_NEAR) &&
	       is_power_of_two(geometry->sector_size) && geometry->sector_size >= SECTOR_SIZE_MIN &&
	       geometry->sector_size <= SECTOR_SIZE_MAX && is_power_of_two(geometry->per_cluster) &&
	       geometry->sector_size * geometry->per_cluster <= CLUSTER_SIZE_MAX && geometry->reserved != 0 &&
	       geometry->copies != 0 && geometry->copies <= FAT_COPIES_MAX && geometry->total != 0 &&
	       geometry->fat_size != 0 && (media == MEDIA_REMOVABLE || media >= MEDIA_FIXED_MIN);
}

static void copy_label(char *label, const uint8_t *raw)
{
	size_t length = LABEL_SIZE;
	size_t i;

	while (length > 0 && raw[length - 1] == ' ') {
		length--;
	}

	for (i = 0; i < length && i + 1 < FS_LABEL_MAX; i++) {
		label[i] = isprint(raw[i]) ? (char)raw[i] : '?';
	}

	label[i] = '\0';
	if (strcmp(label, "NO NAME") == 0) {
		label[0] = '\0';
	}
}

static unsigned int entry_bits(uint64_t clusters)
{
	if (clusters < FAT12_CLUSTERS_MAX) {
		return 12;
	}

	if (clusters < FAT16_CLUSTERS_MAX) {
		return 16;
	}

	return 32;
}

static const char *type_name(unsigned int bits)
{
	switch (bits) {
	case 12:
		return "fat12";
	case 16:
		return "fat16";
	default:
		return "fat32";
	}
}

static int parse_boot_sector(struct fat *fat, const uint8_t *sector, struct fs *fs)
{
	struct geometry geometry;
	uint64_t root_sectors;
	uint64_t first_data;
	uint64_t clusters;

	read_geometry(sector, &geometry);
	if (!geometry_plausible(sector, &geometry)) {
		return -ENOENT;
	}

	root_sectors = ((uint64_t)geometry.root_entries * FAT_ENTRY_SIZE + geometry.sector_size - 1) /
		       geometry.sector_size;
	first_data = geometry.reserved + geometry.copies * geometry.fat_size + root_sectors;
	if (first_data >= geometry.total) {
		return -ENOENT;
	}

	clusters = (geometry.total - first_data) / geometry.per_cluster;
	fat->bits = entry_bits(clusters);

	if ((fat->bits == 32) != (geometry.root_entries == 0) || clusters > FAT32_CLUSTERS_MAX) {
		return -ENOENT;
	}

	if (geometry.fat_size * geometry.sector_size * 8 / fat->bits < clusters + 2 ||
	    geometry.total * geometry.sector_size > blockdev_size(fat->device)) {
		return -EBADMSG;
	}

	fat->sector_size = geometry.sector_size;
	fat->cluster_size = geometry.sector_size * geometry.per_cluster;
	fat->cluster_count = (uint32_t)clusters;
	fat->root_entries = geometry.root_entries;
	fat->fat_offset = (uint64_t)geometry.reserved * geometry.sector_size;
	fat->root_offset = (geometry.reserved + geometry.copies * geometry.fat_size) * geometry.sector_size;
	fat->data_offset = first_data * geometry.sector_size;

	if (fat->bits == 32) {
		fat->root_cluster = get_le32(sector + BPB_ROOT_CLUSTER);
		if (get_le16(sector + BPB_VERSION32) != 0 || fat->root_cluster < 2 ||
		    fat->root_cluster >= fat->cluster_count + 2) {
			return -EBADMSG;
		}
	}

	copy_label(fs->label, sector + (fat->bits == 32 ? BPB_LABEL32 : BPB_LABEL16));
	fs->type = type_name(fat->bits);

	return 0;
}

static int read_fat_byte(struct fat *fat, uint64_t offset, uint8_t *byte)
{
	uint64_t sector = offset / fat->sector_size;

	if (!fat->cache_valid || fat->cached_sector != sector) {
		int err = blockdev_read_bytes(fat->device, sector * fat->sector_size, fat->cache, fat->sector_size);

		if (err != 0) {
			fat->cache_valid = false;
			return err;
		}

		fat->cached_sector = sector;
		fat->cache_valid = true;
	}

	*byte = fat->cache[offset % fat->sector_size];

	return 0;
}

static int read_fat(struct fat *fat, uint64_t offset, unsigned int length, uint32_t *value)
{
	uint8_t raw[4] = { 0 };
	unsigned int i;

	for (i = 0; i < length; i++) {
		int err = read_fat_byte(fat, fat->fat_offset + offset + i, &raw[i]);

		if (err != 0) {
			return err;
		}
	}

	*value = get_le32(raw);

	return 0;
}

int fat_next(struct fat *fat, uint32_t cluster, uint32_t *next)
{
	uint32_t value;
	uint32_t end;
	int err;

	if (cluster < 2 || cluster >= fat->cluster_count + 2) {
		return -EBADMSG;
	}

	switch (fat->bits) {
	case 12:
		err = read_fat(fat, cluster + cluster / 2, 2, &value);
		value = (cluster & 1) != 0 ? value >> 4 : value & 0xfff;
		end = FAT12_END;
		break;
	case 16:
		err = read_fat(fat, (uint64_t)cluster * 2, 2, &value);
		end = FAT16_END;
		break;
	default:
		err = read_fat(fat, (uint64_t)cluster * 4, 4, &value);
		value &= FAT32_MASK;
		end = FAT32_END;
		break;
	}

	if (err != 0) {
		return err;
	}

	if (value >= end) {
		*next = 0;
		return 0;
	}

	if (value < 2 || value >= fat->cluster_count + 2) {
		return -EBADMSG;
	}

	*next = value;

	return 0;
}

uint64_t fat_cluster_offset(const struct fat *fat, uint32_t cluster)
{
	return fat->data_offset + (uint64_t)(cluster - 2) * fat->cluster_size;
}

static enum fs_kind kind_of(const struct fat_node *node)
{
	return node->directory ? FS_KIND_DIRECTORY : FS_KIND_FILE;
}

static int fat_stat(struct fs *fs, const char *path, struct fs_stat *stat)
{
	struct fat_node node;
	int err = fat_lookup(fs->state, path, &node);

	if (err == 0) {
		stat->kind = kind_of(&node);
		stat->size = node.directory ? 0 : node.size;
	}

	return err;
}

static int fat_read(struct fs *fs, const char *path, uint64_t offset, void *buffer, size_t length, size_t *done)
{
	struct fat_node node;
	int err = fat_lookup(fs->state, path, &node);

	if (err != 0) {
		return err;
	}

	if (node.directory) {
		return -EISDIR;
	}

	return fat_read_data(fs->state, &node, offset, buffer, length, done);
}

struct listing {
	fs_visit	visit;
	void		*context;
	struct fs_entry	entry;
};

static int list_entry(void *context, const char *name, const struct fat_node *node)
{
	struct listing *listing = context;

	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
		return 0;
	}

	strlcpy(listing->entry.name, name, sizeof(listing->entry.name));
	listing->entry.kind = kind_of(node);
	listing->entry.size = node->directory ? 0 : node->size;

	return listing->visit(listing->context, &listing->entry);
}

static int fat_list(struct fs *fs, const char *path, fs_visit visit, void *context)
{
	struct listing *listing = malloc(sizeof(*listing));
	struct fat_node directory;
	int err;

	if (listing == NULL) {
		return -ENOMEM;
	}

	listing->visit = visit;
	listing->context = context;

	err = fat_lookup(fs->state, path, &directory);
	if (err == 0 && !directory.directory) {
		err = -ENOTDIR;
	}

	if (err == 0) {
		err = fat_iterate(fs->state, &directory, list_entry, listing);
	}

	free(listing);

	return err;
}

static void fat_release(struct fs *fs)
{
	struct fat *fat = fs->state;

	free(fat->cache);
	free(fat);
}

static const struct fs_ops fat_ops = {
	.stat		= fat_stat,
	.read		= fat_read,
	.list		= fat_list,
	.release	= fat_release,
};

int fat_mount(struct fs *fs)
{
	uint8_t *sector = malloc(BOOT_SECTOR_SIZE);
	struct fat *fat = calloc(1, sizeof(*fat));
	int err;

	if (sector == NULL || fat == NULL) {
		free(sector);
		free(fat);
		return -ENOMEM;
	}

	fat->device = fs->device;
	err = blockdev_read_bytes(fs->device, 0, sector, BOOT_SECTOR_SIZE);
	if (err == -ERANGE) {
		err = -ENOENT;
	}

	if (err == 0) {
		err = parse_boot_sector(fat, sector, fs);
	}

	if (err == 0) {
		fat->cache = malloc(fat->sector_size);
		err = fat->cache != NULL ? 0 : -ENOMEM;
	}

	free(sector);

	if (err != 0) {
		free(fat->cache);
		free(fat);
		return err;
	}

	fs->state = fat;
	fs->ops = &fat_ops;

	return 0;
}
