#include <arch.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/macros.h>
#include <sys/time.h>

#include "private.h"

#define TRANSFER_TIMEOUT_US	5000000

static void sync_for_device(const struct virtio_device *dev, const void *data, size_t length)
{
	if (!dev->coherent && length != 0) {
		arch_dcache_clean_invalidate_range((uintptr_t)data, length);
	}
}

static void sync_for_cpu(const struct virtio_device *dev, const void *data, size_t length)
{
	if (!dev->coherent && length != 0) {
		arch_dcache_invalidate_range((uintptr_t)data, length);
	}
}

int virtqueue_setup(struct virtio_device *dev, struct virtqueue *queue, unsigned int index)
{
	unsigned int maximum = dev->transport->queue_max(dev, index);
	unsigned int size = VIRTQ_SIZE_MAX;
	size_t rings;
	size_t used;
	int err;

	if (maximum == 0) {
		return -ENODEV;
	}

	while (size > maximum) {
		size /= 2;
	}

	rings = sizeof(struct virtq_desc) * size + sizeof(struct virtq_avail) + sizeof(uint16_t) * (size + 1);
	used = ALIGN_UP(rings, VIRTQ_ALIGN);

	memset(queue, 0, sizeof(*queue));
	queue->index = index;
	queue->size = size;
	queue->memory_size = used + ALIGN_UP(sizeof(struct virtq_used) + sizeof(struct virtq_used_element) * size +
					     sizeof(uint16_t), VIRTQ_ALIGN);
	queue->memory = aligned_alloc(VIRTQ_ALIGN, queue->memory_size);
	if (queue->memory == NULL) {
		return -ENOMEM;
	}

	memset(queue->memory, 0, queue->memory_size);
	queue->desc = queue->memory;
	queue->avail = (struct virtq_avail *)((uint8_t *)queue->memory + sizeof(struct virtq_desc) * size);
	queue->used = (struct virtq_used *)((uint8_t *)queue->memory + used);
	sync_for_device(dev, queue->memory, queue->memory_size);

	err = dev->transport->queue_enable(dev, queue);
	if (err != 0) {
		free(queue->memory);
		queue->memory = NULL;
	}

	return err;
}

static void fill_chain(struct virtqueue *queue, const struct virtio_buffer *buffers, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		struct virtq_desc *desc = &queue->desc[i];

		desc->address = (uintptr_t)buffers[i].data;
		desc->length = buffers[i].length;
		desc->flags = (uint16_t)((i + 1 < count ? VIRTQ_DESC_F_NEXT : 0) |
					 (buffers[i].device_writes ? VIRTQ_DESC_F_WRITE : 0));
		desc->next = (uint16_t)(i + 1 < count ? i + 1 : 0);
	}
}

static size_t used_size(const struct virtqueue *queue)
{
	return queue->memory_size - (size_t)((uint8_t *)queue->used - (uint8_t *)queue->memory);
}

static int wait_used(struct virtio_device *dev, struct virtqueue *queue)
{
	uint64_t start = time_us();

	for (;;) {
		sync_for_cpu(dev, queue->used, sizeof(*queue->used));
		if (READ_ONCE(queue->used->index) != queue->last_used) {
			return 0;
		}

		if (time_us() - start > TRANSFER_TIMEOUT_US) {
			return -ETIMEDOUT;
		}
	}
}

int virtqueue_transfer(struct virtio_device *dev, struct virtqueue *queue, const struct virtio_buffer *buffers,
		       unsigned int count, uint32_t *written)
{
	struct virtq_used_element element;
	uint16_t slot;
	unsigned int i;
	int err;

	if (queue->memory == NULL || count == 0 || count > queue->size) {
		return -EINVAL;
	}

	fill_chain(queue, buffers, count);
	for (i = 0; i < count; i++) {
		sync_for_device(dev, buffers[i].data, buffers[i].length);
	}

	slot = queue->avail->index % queue->size;
	queue->avail->ring[slot] = 0;
	arch_memory_barrier();
	WRITE_ONCE(queue->avail->index, (uint16_t)(queue->avail->index + 1));
	sync_for_device(dev, queue->memory, queue->memory_size);
	arch_memory_barrier();
	dev->transport->notify(dev, queue);

	err = wait_used(dev, queue);
	if (err != 0) {
		return err;
	}

	arch_memory_barrier();
	sync_for_cpu(dev, queue->used, used_size(queue));
	element = queue->used->ring[queue->last_used % queue->size];
	queue->last_used++;

	for (i = 0; i < count; i++) {
		if (buffers[i].device_writes) {
			sync_for_cpu(dev, buffers[i].data, buffers[i].length);
		}
	}

	if (element.id != 0) {
		return -EIO;
	}

	if (written != NULL) {
		*written = element.length;
	}

	return 0;
}

int virtqueue_post(struct virtio_device *dev, struct virtqueue *queue, uint16_t id, void *data, uint32_t length)
{
	struct virtq_desc *desc;

	if (queue->memory == NULL || id >= queue->size) {
		return -EINVAL;
	}

	desc = &queue->desc[id];
	desc->address = (uintptr_t)data;
	desc->length = length;
	desc->flags = VIRTQ_DESC_F_WRITE;
	desc->next = 0;
	sync_for_device(dev, data, length);

	queue->avail->ring[queue->avail->index % queue->size] = id;
	arch_memory_barrier();
	WRITE_ONCE(queue->avail->index, (uint16_t)(queue->avail->index + 1));

	return 0;
}

void virtqueue_kick(struct virtio_device *dev, struct virtqueue *queue)
{
	sync_for_device(dev, queue->memory, queue->memory_size);
	arch_memory_barrier();
	dev->transport->notify(dev, queue);
}

int virtqueue_collect(struct virtio_device *dev, struct virtqueue *queue, uint16_t *id, uint32_t *length)
{
	struct virtq_used_element element;
	const struct virtq_desc *desc;

	sync_for_cpu(dev, queue->used, sizeof(*queue->used));
	if (READ_ONCE(queue->used->index) == queue->last_used) {
		return -EAGAIN;
	}

	arch_memory_barrier();
	sync_for_cpu(dev, queue->used, used_size(queue));
	element = queue->used->ring[queue->last_used % queue->size];
	queue->last_used++;

	if (element.id >= queue->size) {
		return -EIO;
	}

	desc = &queue->desc[element.id];
	if (element.length > desc->length) {
		return -EIO;
	}

	sync_for_cpu(dev, (const void *)(uintptr_t)desc->address, desc->length);
	*id = (uint16_t)element.id;
	*length = element.length;

	return 0;
}
