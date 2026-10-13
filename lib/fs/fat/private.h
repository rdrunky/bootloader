#ifndef FAT_PRIVATE_H
#define FAT_PRIVATE_H

#include <stdbool.h>

#include "../private.h"

#define FAT_ENTRY_SIZE		32

struct fat {
	struct blockdev	*device;
	unsigned int	bits;
	uint32_t	sector_size;
	uint32_t	cluster_size;
	uint32_t	cluster_count;
	uint32_t	root_entries;
	uint32_t	root_cluster;
	uint64_t	fat_offset;
	uint64_t	root_offset;
	uint64_t	data_offset;
	uint64_t	cached_sector;
	bool		cache_valid;
	uint8_t		*cache;
};

struct fat_node {
	bool		directory;
	bool		root;
	uint32_t	cluster;
	uint32_t	size;
};

typedef int (*fat_visit)(void *context, const char *name, const struct fat_node *node);

int fat_next(struct fat *fat, uint32_t cluster, uint32_t *next);
uint64_t fat_cluster_offset(const struct fat *fat, uint32_t cluster);
int fat_read_data(struct fat *fat, const struct fat_node *node, uint64_t offset, void *buffer, size_t length,
		  size_t *done);
int fat_iterate(struct fat *fat, const struct fat_node *directory, fat_visit visit, void *context);
int fat_lookup(struct fat *fat, const char *path, struct fat_node *node);

#endif
