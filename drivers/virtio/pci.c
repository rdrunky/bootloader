#include <drivers/pci.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/macros.h>

#include "private.h"

#define VIRTIO_PCI_VENDOR		0x1af4
#define VIRTIO_PCI_DEVICE_FIRST		0x1000
#define VIRTIO_PCI_DEVICE_LAST		0x107f
#define VIRTIO_PCI_MODERN_FIRST		0x1040

#define STRUCTURE_COMMON		1
#define STRUCTURE_NOTIFY		2
#define STRUCTURE_DEVICE		4

#define CAPABILITY_STRUCTURE		3
#define CAPABILITY_BAR			4
#define CAPABILITY_OFFSET		8
#define CAPABILITY_LENGTH		12
#define CAPABILITY_NOTIFY_MULTIPLIER	16

#define COMMON_DEVICE_FEATURE_SELECT	0x00
#define COMMON_DEVICE_FEATURE		0x04
#define COMMON_DRIVER_FEATURE_SELECT	0x08
#define COMMON_DRIVER_FEATURE		0x0c
#define COMMON_QUEUE_COUNT		0x12
#define COMMON_STATUS			0x14
#define COMMON_CONFIG_GENERATION	0x15
#define COMMON_QUEUE_SELECT		0x16
#define COMMON_QUEUE_SIZE		0x18
#define COMMON_QUEUE_ENABLE		0x1c
#define COMMON_QUEUE_NOTIFY_OFFSET	0x1e
#define COMMON_QUEUE_DESC		0x20
#define COMMON_QUEUE_DRIVER		0x28
#define COMMON_QUEUE_DEVICE		0x30
#define COMMON_SIZE			0x38

#define QUEUES_MAX			8
#define NOTIFY_SIZE			2

struct region {
	uintptr_t	address;
	uint32_t	length;
};

struct virtio_pci {
	struct virtio_device	virtio;
	struct region		common;
	struct region		notify;
	struct region		config;
	uint32_t		notify_multiplier;
	uint16_t		notify_offsets[QUEUES_MAX];
};

static struct virtio_pci *transport_of(struct virtio_device *dev)
{
	return container_of(dev, struct virtio_pci, virtio);
}

static uint8_t read8(uintptr_t address)
{
	return *(volatile uint8_t *)address;
}

static uint16_t read16(uintptr_t address)
{
	return *(volatile uint16_t *)address;
}

static uint32_t read32(uintptr_t address)
{
	return *(volatile uint32_t *)address;
}

static void write8(uintptr_t address, uint8_t value)
{
	*(volatile uint8_t *)address = value;
}

static void write16(uintptr_t address, uint16_t value)
{
	*(volatile uint16_t *)address = value;
}

static void write32(uintptr_t address, uint32_t value)
{
	*(volatile uint32_t *)address = value;
}

static void write_address(uintptr_t address, const void *pointer)
{
	uint64_t value = (uintptr_t)pointer;

	write32(address, (uint32_t)value);
	write32(address + 4, (uint32_t)(value >> 32));
}

static uint32_t pci_config_read(struct virtio_device *dev, unsigned int offset)
{
	struct virtio_pci *transport = transport_of(dev);

	if (offset + sizeof(uint32_t) > transport->config.length) {
		return 0;
	}

	return read32(transport->config.address + offset);
}

static uint32_t pci_config_generation(struct virtio_device *dev)
{
	return read8(transport_of(dev)->common.address + COMMON_CONFIG_GENERATION);
}

static uint8_t pci_get_status(struct virtio_device *dev)
{
	return read8(transport_of(dev)->common.address + COMMON_STATUS);
}

static void pci_set_status(struct virtio_device *dev, uint8_t status)
{
	write8(transport_of(dev)->common.address + COMMON_STATUS, status);
}

static uint64_t pci_get_features(struct virtio_device *dev)
{
	uintptr_t common = transport_of(dev)->common.address;
	uint64_t features;

	write32(common + COMMON_DEVICE_FEATURE_SELECT, 0);
	features = read32(common + COMMON_DEVICE_FEATURE);
	write32(common + COMMON_DEVICE_FEATURE_SELECT, 1);

	return features | (uint64_t)read32(common + COMMON_DEVICE_FEATURE) << 32;
}

static void pci_set_features(struct virtio_device *dev, uint64_t features)
{
	uintptr_t common = transport_of(dev)->common.address;

	write32(common + COMMON_DRIVER_FEATURE_SELECT, 0);
	write32(common + COMMON_DRIVER_FEATURE, (uint32_t)features);
	write32(common + COMMON_DRIVER_FEATURE_SELECT, 1);
	write32(common + COMMON_DRIVER_FEATURE, (uint32_t)(features >> 32));
}

static unsigned int pci_queue_max(struct virtio_device *dev, unsigned int index)
{
	uintptr_t common = transport_of(dev)->common.address;

	if (index >= QUEUES_MAX || index >= read16(common + COMMON_QUEUE_COUNT)) {
		return 0;
	}

	write16(common + COMMON_QUEUE_SELECT, (uint16_t)index);
	if (read16(common + COMMON_QUEUE_ENABLE) != 0) {
		return 0;
	}

	return read16(common + COMMON_QUEUE_SIZE);
}

static int pci_queue_enable(struct virtio_device *dev, struct virtqueue *queue)
{
	struct virtio_pci *transport = transport_of(dev);
	uintptr_t common = transport->common.address;
	uint16_t offset;

	write16(common + COMMON_QUEUE_SELECT, (uint16_t)queue->index);
	write16(common + COMMON_QUEUE_SIZE, (uint16_t)queue->size);
	write_address(common + COMMON_QUEUE_DESC, queue->desc);
	write_address(common + COMMON_QUEUE_DRIVER, queue->avail);
	write_address(common + COMMON_QUEUE_DEVICE, queue->used);

	offset = read16(common + COMMON_QUEUE_NOTIFY_OFFSET);
	if ((uint64_t)offset * transport->notify_multiplier + NOTIFY_SIZE > transport->notify.length) {
		return -EIO;
	}

	transport->notify_offsets[queue->index] = offset;
	write16(common + COMMON_QUEUE_ENABLE, 1);

	return read16(common + COMMON_QUEUE_ENABLE) == 1 ? 0 : -EIO;
}

static void pci_notify(struct virtio_device *dev, struct virtqueue *queue)
{
	struct virtio_pci *transport = transport_of(dev);
	uintptr_t offset = (uintptr_t)transport->notify_offsets[queue->index] * transport->notify_multiplier;

	write16(transport->notify.address + offset, (uint16_t)queue->index);
}

static const struct virtio_transport pci_transport = {
	.config_read		= pci_config_read,
	.config_generation	= pci_config_generation,
	.get_status		= pci_get_status,
	.set_status		= pci_set_status,
	.get_features		= pci_get_features,
	.set_features		= pci_set_features,
	.queue_max		= pci_queue_max,
	.queue_enable		= pci_queue_enable,
	.notify			= pci_notify,
};

static int find_structure(const struct pci_device *pci, uint8_t type, struct region *region, int *capability)
{
	int at;

	for (at = pci_next_capability(pci, PCI_CAPABILITY_VENDOR, -1); at >= 0;
	     at = pci_next_capability(pci, PCI_CAPABILITY_VENDOR, at)) {
		unsigned int bar = pci_read8(pci, (unsigned int)at + CAPABILITY_BAR);
		uint32_t offset = pci_read32(pci, (unsigned int)at + CAPABILITY_OFFSET);
		uint32_t length = pci_read32(pci, (unsigned int)at + CAPABILITY_LENGTH);

		if (pci_read8(pci, (unsigned int)at + CAPABILITY_STRUCTURE) != type || bar >= PCI_BARS_MAX ||
		    pci->bars[bar].space != PCI_SPACE_MEMORY || pci->bars[bar].address == 0 ||
		    (uint64_t)offset + length > pci->bars[bar].size) {
			continue;
		}

		region->address = (uintptr_t)(pci->bars[bar].address + offset);
		region->length = length;
		*capability = at;
		return 0;
	}

	return -ENODEV;
}

static uint32_t device_type(const struct pci_device *pci)
{
	if (pci->device >= VIRTIO_PCI_MODERN_FIRST) {
		return pci->device - VIRTIO_PCI_MODERN_FIRST;
	}

	return pci_read16(pci, PCI_SUBSYSTEM_ID);
}

static int pci_probe(struct pci_device *pci)
{
	struct virtio_pci *transport;
	uint32_t type = device_type(pci);
	int capability;
	int err;

	if (!virtio_supported(type)) {
		return -ENODEV;
	}

	transport = calloc(1, sizeof(*transport));
	if (transport == NULL) {
		return -ENOMEM;
	}

	transport->virtio.device_id = type;
	transport->virtio.coherent = pci->coherent;
	transport->virtio.transport = &pci_transport;

	err = pci_enable(pci);
	if (err == 0) {
		err = find_structure(pci, STRUCTURE_COMMON, &transport->common, &capability);
	}

	if (err == 0 && transport->common.length < COMMON_SIZE) {
		err = -ENODEV;
	}

	if (err == 0) {
		err = find_structure(pci, STRUCTURE_NOTIFY, &transport->notify, &capability);
	}

	if (err == 0) {
		transport->notify_multiplier = pci_read32(pci, (unsigned int)capability + CAPABILITY_NOTIFY_MULTIPLIER);
		if (find_structure(pci, STRUCTURE_DEVICE, &transport->config, &capability) != 0) {
			transport->config.length = 0;
		}
		err = virtio_attach(&transport->virtio);
	}

	if (err != 0) {
		free(transport);
	}

	return err;
}

static const struct pci_driver virtio_pci_driver = {
	.name		= "virtio-pci",
	.vendor		= VIRTIO_PCI_VENDOR,
	.device_first	= VIRTIO_PCI_DEVICE_FIRST,
	.device_last	= VIRTIO_PCI_DEVICE_LAST,
	.probe		= pci_probe,
};

PCI_DRIVER(virtio_pci_driver);
