#ifndef LIB_BLOCKDEV_H
#define LIB_BLOCKDEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLOCKDEV_NAME_MAX	16
#define BLOCKDEV_LABEL_MAX	40
#define BLOCKDEV_GUID_SIZE	16

struct blockdev;

struct blockdev_ops {
	int	(*read)(struct blockdev *dev, uint64_t block, uint32_t count, void *buffer);
	int	(*write)(struct blockdev *dev, uint64_t block, uint32_t count, const void *buffer);
};

struct blockdev {
	char				name[BLOCKDEV_NAME_MAX];
	char				label[BLOCKDEV_LABEL_MAX];
	uint32_t			block_size;
	uint64_t			block_count;
	bool				read_only;
	const struct blockdev_ops	*ops;
	void				*context;
	struct blockdev			*parent;
	uint64_t			first_block;
	unsigned int			index;
	uint8_t				mbr_type;
	uint8_t				type[BLOCKDEV_GUID_SIZE];
	uint8_t				uuid[BLOCKDEV_GUID_SIZE];
	struct blockdev			*next;
};

int blockdev_read(struct blockdev *dev, uint64_t block, uint32_t count, void *buffer);
int blockdev_write(struct blockdev *dev, uint64_t block, uint32_t count, const void *buffer);
int blockdev_read_bytes(struct blockdev *dev, uint64_t offset, void *buffer, size_t length);
uint64_t blockdev_size(const struct blockdev *dev);

int blockdev_init_memory(struct blockdev *dev, const char *name, void *data, uint64_t size, uint32_t block_size,
			 bool read_only);
int blockdev_init_partition(struct blockdev *part, struct blockdev *parent, unsigned int index, uint64_t first,
			    uint64_t count);

void blockdev_register(struct blockdev *dev);
struct blockdev *blockdev_find(const char *name);
struct blockdev *blockdev_list(void);

#endif
