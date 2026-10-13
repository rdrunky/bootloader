#include <errno.h>
#include <lib/blockdev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/storage.h>

#include "private.h"

#define BLOCK_F_SIZE_MAX	(1ULL << 1)
#define BLOCK_F_RO		(1ULL << 5)
#define BLOCK_TYPE_IN		0
#define BLOCK_TYPE_OUT		1
#define BLOCK_STATUS_OK		0
#define BLOCK_STATUS_PENDING	0xff
#define BLOCK_CAPACITY		0
#define BLOCK_SIZE_MAX		8
#define SECTOR_SIZE		512
#define REQUEST_SECTORS_MAX	2048

struct block_request {
	uint32_t	type;
	uint32_t	reserved;
	uint64_t	sector;
	uint8_t		status;
};

struct virtio_block {
	struct blockdev		device;
	struct virtio_device	*virtio;
	struct virtqueue	queue;
	struct block_request	*request;
	uint32_t		request_sectors;
};

static unsigned int disk_count;

static int transfer(struct virtio_block *block, uint32_t type, uint64_t sector, void *data, uint32_t sectors)
{
	struct block_request *request = block->request;
	struct virtio_buffer buffers[3] = {
		{ request, offsetof(struct block_request, status), false },
		{ data, sectors * SECTOR_SIZE, type == BLOCK_TYPE_IN },
		{ &request->status, sizeof(request->status), true },
	};
	int err;

	request->type = type;
	request->reserved = 0;
	request->sector = sector;
	request->status = BLOCK_STATUS_PENDING;

	err = virtqueue_transfer(block->virtio, &block->queue, buffers, 3, NULL);
	if (err != 0) {
		return err;
	}

	return request->status == BLOCK_STATUS_OK ? 0 : -EIO;
}

static int block_io(struct blockdev *dev, uint32_t type, uint64_t sector, uint8_t *data, uint32_t count)
{
	struct virtio_block *block = dev->context;

	while (count > 0) {
		uint32_t sectors = count < block->request_sectors ? count : block->request_sectors;
		int err = transfer(block, type, sector, data, sectors);

		if (err != 0) {
			return err;
		}

		sector += sectors;
		data += (size_t)sectors * SECTOR_SIZE;
		count -= sectors;
	}

	return 0;
}

static int block_read(struct blockdev *dev, uint64_t block, uint32_t count, void *buffer)
{
	return block_io(dev, BLOCK_TYPE_IN, block, buffer, count);
}

static int block_write(struct blockdev *dev, uint64_t block, uint32_t count, const void *buffer)
{
	return block_io(dev, BLOCK_TYPE_OUT, block, (uint8_t *)(uintptr_t)buffer, count);
}

static const struct blockdev_ops block_ops = {
	.read	= block_read,
	.write	= block_write,
};

static uint32_t request_sectors(struct virtio_device *virtio)
{
	uint32_t size_max;

	if ((virtio->features & BLOCK_F_SIZE_MAX) == 0) {
		return REQUEST_SECTORS_MAX;
	}

	size_max = virtio_config_read32(virtio, BLOCK_SIZE_MAX) / SECTOR_SIZE;
	if (size_max == 0) {
		return 1;
	}

	return size_max < REQUEST_SECTORS_MAX ? size_max : REQUEST_SECTORS_MAX;
}

static int block_probe(struct virtio_device *virtio)
{
	struct virtio_block *block = calloc(1, sizeof(*block));
	int err;

	if (block == NULL) {
		return -ENOMEM;
	}

	block->virtio = virtio;
	block->request = aligned_alloc(16, sizeof(*block->request));
	err = block->request != NULL ? virtqueue_setup(virtio, &block->queue, 0) : -ENOMEM;
	if (err != 0) {
		free(block->request);
		free(block);
		return err;
	}

	block->device.block_size = SECTOR_SIZE;
	block->device.block_count = virtio_config_read64(virtio, BLOCK_CAPACITY);
	block->device.read_only = (virtio->features & BLOCK_F_RO) != 0;
	block->request_sectors = request_sectors(virtio);
	block->device.ops = &block_ops;
	block->device.context = block;
	virtio->context = block;

	return 0;
}

static void block_start(struct virtio_device *virtio)
{
	struct virtio_block *block = virtio->context;

	snprintf(block->device.name, sizeof(block->device.name), "virtio%u", disk_count++);
	storage_attach(&block->device);
}

static const struct virtio_driver block_driver = {
	.device_id	= VIRTIO_DEVICE_BLOCK,
	.features	= BLOCK_F_SIZE_MAX | BLOCK_F_RO,
	.probe		= block_probe,
	.start		= block_start,
};

VIRTIO_DRIVER(block_driver);
