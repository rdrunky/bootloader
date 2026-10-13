#include <errno.h>
#include <stddef.h>
#include <sys/driver.h>
#include <sys/time.h>

#include "private.h"

#define GENERATION_RETRIES	8
#define RESET_TIMEOUT_US	1000000

SET_DECLARE(virtio_drivers, const struct virtio_driver);

static const struct virtio_driver *find_driver(uint32_t device_id)
{
	const struct virtio_driver *driver;

	SET_FOREACH(driver, virtio_drivers) {
		if (driver->device_id == device_id) {
			return driver;
		}
	}

	return NULL;
}

bool virtio_supported(uint32_t device_id)
{
	return find_driver(device_id) != NULL;
}

static int reset(struct virtio_device *dev)
{
	uint64_t start = time_us();

	dev->transport->set_status(dev, 0);

	while (dev->transport->get_status(dev) != 0) {
		if (time_us() - start > RESET_TIMEOUT_US) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static void quiesce(void *context)
{
	reset(context);
}

static int negotiate(struct virtio_device *dev, const struct virtio_driver *driver)
{
	const struct virtio_transport *transport = dev->transport;
	uint64_t wanted = driver->features | (dev->legacy ? 0 : VIRTIO_F_VERSION_1);

	dev->features = transport->get_features(dev) & wanted;
	if (!dev->legacy && (dev->features & VIRTIO_F_VERSION_1) == 0) {
		return -ENOTSUP;
	}

	transport->set_features(dev, dev->features);
	if (dev->legacy) {
		return 0;
	}

	transport->set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

	return (transport->get_status(dev) & VIRTIO_STATUS_FEATURES_OK) != 0 ? 0 : -ENOTSUP;
}

int virtio_attach(struct virtio_device *dev)
{
	const struct virtio_transport *transport = dev->transport;
	const struct virtio_driver *driver = find_driver(dev->device_id);
	uint8_t running = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK;
	int err;

	if (driver == NULL) {
		return -ENODEV;
	}

	err = reset(dev);
	if (err == 0) {
		err = driver_quiesce_register(quiesce, dev);
	}

	if (err != 0) {
		return err;
	}

	transport->set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE);
	transport->set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

	err = negotiate(dev, driver);
	if (err == 0) {
		err = driver->probe(dev);
	}

	if (err != 0) {
		transport->set_status(dev, VIRTIO_STATUS_FAILED);
		driver_quiesce_unregister(quiesce, dev);
		return err;
	}

	transport->set_status(dev, running | (dev->legacy ? 0 : VIRTIO_STATUS_FEATURES_OK));

	if (driver->start != NULL) {
		driver->start(dev);
	}

	return 0;
}

uint32_t virtio_config_read32(struct virtio_device *dev, unsigned int offset)
{
	return dev->transport->config_read(dev, offset);
}

uint64_t virtio_config_read64(struct virtio_device *dev, unsigned int offset)
{
	unsigned int attempt;
	uint64_t value = 0;

	for (attempt = 0; attempt < GENERATION_RETRIES; attempt++) {
		uint32_t before = dev->transport->config_generation(dev);

		value = virtio_config_read32(dev, offset) | (uint64_t)virtio_config_read32(dev, offset + 4) << 32;
		if (dev->transport->config_generation(dev) == before) {
			break;
		}
	}

	return value;
}
