#include <errno.h>
#include <lib/blockdev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct blockdev *devices;

static bool range_valid(const struct blockdev *dev, uint64_t block, uint32_t count)
{
	return count != 0 && block <= dev->block_count && count <= dev->block_count - block;
}

int blockdev_read(struct blockdev *dev, uint64_t block, uint32_t count, void *buffer)
{
	if (dev == NULL || dev->ops == NULL || dev->ops->read == NULL || buffer == NULL) {
		return -EINVAL;
	}

	if (!range_valid(dev, block, count)) {
		return -ERANGE;
	}

	return dev->ops->read(dev, block, count, buffer);
}

int blockdev_write(struct blockdev *dev, uint64_t block, uint32_t count, const void *buffer)
{
	if (dev == NULL || dev->ops == NULL || buffer == NULL) {
		return -EINVAL;
	}

	if (dev->read_only || dev->ops->write == NULL) {
		return -EROFS;
	}

	if (!range_valid(dev, block, count)) {
		return -ERANGE;
	}

	return dev->ops->write(dev, block, count, buffer);
}

uint64_t blockdev_size(const struct blockdev *dev)
{
	return dev->block_count * dev->block_size;
}

static int read_partial(struct blockdev *dev, uint8_t *bounce, uint64_t block, size_t skip, uint8_t *buffer,
			size_t length)
{
	int err = blockdev_read(dev, block, 1, bounce);

	if (err == 0) {
		memcpy(buffer, bounce + skip, length);
	}

	return err;
}

int blockdev_read_bytes(struct blockdev *dev, uint64_t offset, void *buffer, size_t length)
{
	uint8_t *out = buffer;
	uint8_t *bounce = NULL;
	uint64_t block;
	size_t skip;
	int err = 0;

	if (dev == NULL || dev->block_size == 0 || buffer == NULL) {
		return -EINVAL;
	}

	if (length == 0) {
		return 0;
	}

	if (offset > blockdev_size(dev) || length > blockdev_size(dev) - offset) {
		return -ERANGE;
	}

	block = offset / dev->block_size;
	skip = (size_t)(offset % dev->block_size);

	if (skip != 0 || length < dev->block_size) {
		size_t part = dev->block_size - skip < length ? dev->block_size - skip : length;

		bounce = malloc(dev->block_size);
		if (bounce == NULL) {
			return -ENOMEM;
		}

		err = read_partial(dev, bounce, block, skip, out, part);
		out += part;
		length -= part;
		block++;
	}

	if (err == 0 && length >= dev->block_size) {
		uint64_t whole = length / dev->block_size;

		while (err == 0 && whole > 0) {
			uint32_t count = whole > UINT32_MAX ? UINT32_MAX : (uint32_t)whole;

			err = blockdev_read(dev, block, count, out);
			out += (size_t)count * dev->block_size;
			length -= (size_t)count * dev->block_size;
			block += count;
			whole -= count;
		}
	}

	if (err == 0 && length > 0) {
		if (bounce == NULL) {
			bounce = malloc(dev->block_size);
		}

		err = bounce != NULL ? read_partial(dev, bounce, block, 0, out, length) : -ENOMEM;
	}

	free(bounce);

	return err;
}

static int memory_read(struct blockdev *dev, uint64_t block, uint32_t count, void *buffer)
{
	memcpy(buffer, (uint8_t *)dev->context + block * dev->block_size, (size_t)count * dev->block_size);

	return 0;
}

static int memory_write(struct blockdev *dev, uint64_t block, uint32_t count, const void *buffer)
{
	memcpy((uint8_t *)dev->context + block * dev->block_size, buffer, (size_t)count * dev->block_size);

	return 0;
}

static const struct blockdev_ops memory_ops = {
	.read	= memory_read,
	.write	= memory_write,
};

int blockdev_init_memory(struct blockdev *dev, const char *name, void *data, uint64_t size, uint32_t block_size,
			 bool read_only)
{
	if (data == NULL || block_size == 0 || (block_size & (block_size - 1)) != 0 || size < block_size) {
		return -EINVAL;
	}

	memset(dev, 0, sizeof(*dev));
	strlcpy(dev->name, name, sizeof(dev->name));
	dev->block_size = block_size;
	dev->block_count = size / block_size;
	dev->read_only = read_only;
	dev->ops = &memory_ops;
	dev->context = data;

	return 0;
}

static int partition_read(struct blockdev *part, uint64_t block, uint32_t count, void *buffer)
{
	return blockdev_read(part->parent, part->first_block + block, count, buffer);
}

static int partition_write(struct blockdev *part, uint64_t block, uint32_t count, const void *buffer)
{
	return blockdev_write(part->parent, part->first_block + block, count, buffer);
}

static const struct blockdev_ops partition_ops = {
	.read	= partition_read,
	.write	= partition_write,
};

int blockdev_init_partition(struct blockdev *part, struct blockdev *parent, unsigned int index, uint64_t first,
			    uint64_t count)
{
	char name[BLOCKDEV_NAME_MAX];
	int length;

	if (count == 0 || first > parent->block_count || count > parent->block_count - first) {
		return -ERANGE;
	}

	length = snprintf(name, sizeof(name), "%sp%u", parent->name, index);
	if (length < 0 || (size_t)length >= sizeof(name)) {
		return -ENAMETOOLONG;
	}

	memset(part, 0, sizeof(*part));
	strlcpy(part->name, name, sizeof(part->name));
	part->block_size = parent->block_size;
	part->block_count = count;
	part->read_only = parent->read_only;
	part->ops = &partition_ops;
	part->parent = parent;
	part->first_block = first;
	part->index = index;

	return 0;
}

void blockdev_register(struct blockdev *dev)
{
	struct blockdev **tail = &devices;

	while (*tail != NULL) {
		if (*tail == dev) {
			return;
		}
		tail = &(*tail)->next;
	}

	dev->next = NULL;
	*tail = dev;
}

struct blockdev *blockdev_find(const char *name)
{
	struct blockdev *dev;

	for (dev = devices; dev != NULL; dev = dev->next) {
		if (strcmp(dev->name, name) == 0) {
			return dev;
		}
	}

	return NULL;
}

struct blockdev *blockdev_list(void)
{
	return devices;
}
