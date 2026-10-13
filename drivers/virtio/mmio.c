#include <errno.h>
#include <stdlib.h>
#include <sys/driver.h>
#include <sys/mem.h>

#include "private.h"

#define MMIO_MAGIC			0x000
#define MMIO_VERSION			0x004
#define MMIO_DEVICE_ID			0x008
#define MMIO_DEVICE_FEATURES		0x010
#define MMIO_DEVICE_FEATURES_SEL	0x014
#define MMIO_DRIVER_FEATURES		0x020
#define MMIO_DRIVER_FEATURES_SEL	0x024
#define MMIO_GUEST_PAGE_SIZE		0x028
#define MMIO_QUEUE_SEL			0x030
#define MMIO_QUEUE_NUM_MAX		0x034
#define MMIO_QUEUE_NUM			0x038
#define MMIO_QUEUE_ALIGN		0x03c
#define MMIO_QUEUE_PFN			0x040
#define MMIO_QUEUE_READY		0x044
#define MMIO_QUEUE_NOTIFY		0x050
#define MMIO_STATUS			0x070
#define MMIO_QUEUE_DESC			0x080
#define MMIO_QUEUE_DRIVER		0x090
#define MMIO_QUEUE_DEVICE		0x0a0
#define MMIO_CONFIG_GENERATION		0x0fc
#define MMIO_CONFIG			0x100

#define MMIO_MAGIC_VALUE		0x74726976
#define MMIO_VERSION_LEGACY		1
#define MMIO_VERSION_MODERN		2
#define MMIO_PAGE_SIZE			4096

static uint32_t mmio_read(const struct virtio_device *dev, unsigned int reg)
{
	return *(volatile uint32_t *)(dev->base + reg);
}

static void mmio_write(const struct virtio_device *dev, unsigned int reg, uint32_t value)
{
	*(volatile uint32_t *)(dev->base + reg) = value;
}

static uint32_t mmio_config_read(struct virtio_device *dev, unsigned int offset)
{
	return mmio_read(dev, MMIO_CONFIG + offset);
}

static uint32_t mmio_config_generation(struct virtio_device *dev)
{
	return dev->legacy ? 0 : mmio_read(dev, MMIO_CONFIG_GENERATION);
}

static uint8_t mmio_get_status(struct virtio_device *dev)
{
	return (uint8_t)mmio_read(dev, MMIO_STATUS);
}

static void mmio_set_status(struct virtio_device *dev, uint8_t status)
{
	mmio_write(dev, MMIO_STATUS, status);
}

static uint64_t mmio_get_features(struct virtio_device *dev)
{
	uint64_t features;

	mmio_write(dev, MMIO_DEVICE_FEATURES_SEL, 0);
	features = mmio_read(dev, MMIO_DEVICE_FEATURES);
	mmio_write(dev, MMIO_DEVICE_FEATURES_SEL, 1);

	return features | (uint64_t)mmio_read(dev, MMIO_DEVICE_FEATURES) << 32;
}

static void mmio_set_features(struct virtio_device *dev, uint64_t features)
{
	mmio_write(dev, MMIO_DRIVER_FEATURES_SEL, 0);
	mmio_write(dev, MMIO_DRIVER_FEATURES, (uint32_t)features);
	mmio_write(dev, MMIO_DRIVER_FEATURES_SEL, 1);
	mmio_write(dev, MMIO_DRIVER_FEATURES, (uint32_t)(features >> 32));
}

static unsigned int mmio_queue_max(struct virtio_device *dev, unsigned int index)
{
	mmio_write(dev, MMIO_QUEUE_SEL, index);

	if (!dev->legacy && mmio_read(dev, MMIO_QUEUE_READY) != 0) {
		return 0;
	}

	return mmio_read(dev, MMIO_QUEUE_NUM_MAX);
}

static void write_address(struct virtio_device *dev, unsigned int reg, const void *address)
{
	uint64_t value = (uintptr_t)address;

	mmio_write(dev, reg, (uint32_t)value);
	mmio_write(dev, reg + 4, (uint32_t)(value >> 32));
}

static int mmio_queue_enable(struct virtio_device *dev, struct virtqueue *queue)
{
	mmio_write(dev, MMIO_QUEUE_SEL, queue->index);
	mmio_write(dev, MMIO_QUEUE_NUM, queue->size);

	if (dev->legacy) {
		uint64_t frame = (uintptr_t)queue->memory / MMIO_PAGE_SIZE;

		if (frame > UINT32_MAX) {
			return -ERANGE;
		}

		mmio_write(dev, MMIO_GUEST_PAGE_SIZE, MMIO_PAGE_SIZE);
		mmio_write(dev, MMIO_QUEUE_ALIGN, VIRTQ_ALIGN);
		mmio_write(dev, MMIO_QUEUE_PFN, (uint32_t)frame);
		return 0;
	}

	write_address(dev, MMIO_QUEUE_DESC, queue->desc);
	write_address(dev, MMIO_QUEUE_DRIVER, queue->avail);
	write_address(dev, MMIO_QUEUE_DEVICE, queue->used);
	mmio_write(dev, MMIO_QUEUE_READY, 1);

	return mmio_read(dev, MMIO_QUEUE_READY) == 1 ? 0 : -EIO;
}

static void mmio_notify(struct virtio_device *dev, struct virtqueue *queue)
{
	mmio_write(dev, MMIO_QUEUE_NOTIFY, queue->index);
}

static const struct virtio_transport mmio_transport = {
	.config_read		= mmio_config_read,
	.config_generation	= mmio_config_generation,
	.get_status		= mmio_get_status,
	.set_status		= mmio_set_status,
	.get_features		= mmio_get_features,
	.set_features		= mmio_set_features,
	.queue_max		= mmio_queue_max,
	.queue_enable		= mmio_queue_enable,
	.notify			= mmio_notify,
};

static int mmio_probe(const struct dt *dt, int node)
{
	struct virtio_device probe = { .transport = &mmio_transport };
	struct virtio_device *dev;
	uint64_t base;
	uint64_t size;
	uint32_t version;
	int err = dt_reg(dt, node, 0, &base, &size);

	if (err == 0) {
		err = mem_map_device(base, size);
	}

	if (err != 0) {
		return err;
	}

	probe.base = (uintptr_t)base;
	if (mmio_read(&probe, MMIO_MAGIC) != MMIO_MAGIC_VALUE) {
		return -ENODEV;
	}

	version = mmio_read(&probe, MMIO_VERSION);
	if (version != MMIO_VERSION_LEGACY && version != MMIO_VERSION_MODERN) {
		return -ENOTSUP;
	}

	probe.device_id = mmio_read(&probe, MMIO_DEVICE_ID);
	if (probe.device_id == 0) {
		return -ENODEV;
	}

	dev = malloc(sizeof(*dev));
	if (dev == NULL) {
		return -ENOMEM;
	}

	*dev = probe;
	dev->legacy = version == MMIO_VERSION_LEGACY;
	dev->coherent = dt_property(dt, node, "dma-coherent", NULL) != NULL;

	err = virtio_attach(dev);
	if (err != 0) {
		free(dev);
	}

	return err;
}

static const char *const mmio_compatible[] = { "virtio,mmio", NULL };

static const struct driver mmio_driver = {
	.name		= "virtio-mmio",
	.compatible	= mmio_compatible,
	.probe		= mmio_probe,
};

DRIVER(mmio_driver);
