#ifndef VIRTIO_PRIVATE_H
#define VIRTIO_PRIVATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/linkerset.h>

#define VIRTIO_DEVICE_NET		1
#define VIRTIO_DEVICE_BLOCK		2
#define VIRTIO_DEVICE_RANDOM		4
#define VIRTIO_DEVICE_INPUT		18

#define VIRTIO_STATUS_ACKNOWLEDGE	0x01
#define VIRTIO_STATUS_DRIVER		0x02
#define VIRTIO_STATUS_DRIVER_OK		0x04
#define VIRTIO_STATUS_FEATURES_OK	0x08
#define VIRTIO_STATUS_FAILED		0x80

#define VIRTIO_F_VERSION_1		(1ULL << 32)

#define VIRTQ_DESC_F_NEXT		1
#define VIRTQ_DESC_F_WRITE		2
#define VIRTQ_SIZE_MAX			64
#define VIRTQ_ALIGN			4096

struct virtio_device;

struct virtq_desc {
	uint64_t	address;
	uint32_t	length;
	uint16_t	flags;
	uint16_t	next;
};

struct virtq_avail {
	uint16_t	flags;
	uint16_t	index;
	uint16_t	ring[];
};

struct virtq_used_element {
	uint32_t	id;
	uint32_t	length;
};

struct virtq_used {
	uint16_t			flags;
	uint16_t			index;
	struct virtq_used_element	ring[];
};

struct virtqueue {
	unsigned int		index;
	unsigned int		size;
	void			*memory;
	size_t			memory_size;
	struct virtq_desc	*desc;
	struct virtq_avail	*avail;
	struct virtq_used	*used;
	uint16_t		last_used;
};

struct virtio_buffer {
	void		*data;
	uint32_t	length;
	bool		device_writes;
};

struct virtio_transport {
	uint32_t	(*config_read)(struct virtio_device *dev, unsigned int offset);
	uint32_t	(*config_generation)(struct virtio_device *dev);
	uint8_t		(*get_status)(struct virtio_device *dev);
	void		(*set_status)(struct virtio_device *dev, uint8_t status);
	uint64_t	(*get_features)(struct virtio_device *dev);
	void		(*set_features)(struct virtio_device *dev, uint64_t features);
	unsigned int	(*queue_max)(struct virtio_device *dev, unsigned int index);
	int		(*queue_enable)(struct virtio_device *dev, struct virtqueue *queue);
	void		(*notify)(struct virtio_device *dev, struct virtqueue *queue);
};

struct virtio_device {
	uint32_t			device_id;
	uint64_t			features;
	bool				legacy;
	bool				coherent;
	uintptr_t			base;
	const struct virtio_transport	*transport;
	void				*context;
};

struct virtio_driver {
	uint32_t	device_id;
	uint64_t	features;
	int		(*probe)(struct virtio_device *dev);
	void		(*start)(struct virtio_device *dev);
};

#define VIRTIO_DRIVER(driver)	SET_ENTRY(virtio_drivers, driver)

bool virtio_supported(uint32_t device_id);
int virtio_attach(struct virtio_device *dev);
uint32_t virtio_config_read32(struct virtio_device *dev, unsigned int offset);
uint64_t virtio_config_read64(struct virtio_device *dev, unsigned int offset);

int virtqueue_setup(struct virtio_device *dev, struct virtqueue *queue, unsigned int index);
int virtqueue_transfer(struct virtio_device *dev, struct virtqueue *queue, const struct virtio_buffer *buffers,
		       unsigned int count, uint32_t *written);
int virtqueue_post(struct virtio_device *dev, struct virtqueue *queue, uint16_t id, void *data, uint32_t length);
void virtqueue_kick(struct virtio_device *dev, struct virtqueue *queue);
int virtqueue_collect(struct virtio_device *dev, struct virtqueue *queue, uint16_t *id, uint32_t *length);

#endif
