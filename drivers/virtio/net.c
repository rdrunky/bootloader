#include <errno.h>
#include <lib/net.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>

#include "private.h"

#define NET_F_MAC		(1ULL << 5)
#define NET_F_STATUS		(1ULL << 16)
#define VIRTIO_F_ANY_LAYOUT	(1ULL << 27)

#define NET_CONFIG_MAC		0
#define NET_CONFIG_STATUS	4
#define NET_STATUS_SHIFT	16
#define NET_STATUS_LINK_UP	1

#define RECEIVE_QUEUE		0
#define TRANSMIT_QUEUE		1
#define HEADER_LEGACY		10
#define HEADER_MODERN		12
#define BUFFER_SIZE		1536
#define BUFFER_ALIGN		64
#define LOCAL_MAC_PREFIX	0x02

struct virtio_network {
	struct netdev		netdev;
	struct virtio_device	*virtio;
	struct virtqueue	receive;
	struct virtqueue	transmit;
	size_t			header;
	uint8_t			*buffers;
	uint8_t			*staging;
};

static unsigned int generated_macs;

static uint8_t *receive_buffer(const struct virtio_network *network, uint16_t id)
{
	return network->buffers + (size_t)id * BUFFER_SIZE;
}

static int network_send(struct netdev *netdev, const void *frame, size_t length)
{
	struct virtio_network *network = netdev->context;
	struct virtio_buffer buffer = {
		.data = network->staging,
		.length = (uint32_t)(network->header + length),
		.device_writes = false,
	};

	if (network->header + length > BUFFER_SIZE) {
		return -EMSGSIZE;
	}

	memset(network->staging, 0, network->header);
	memcpy(network->staging + network->header, frame, length);

	return virtqueue_transfer(network->virtio, &network->transmit, &buffer, 1, NULL);
}

static void repost(struct virtio_network *network, uint16_t id)
{
	virtqueue_post(network->virtio, &network->receive, id, receive_buffer(network, id), BUFFER_SIZE);
	virtqueue_kick(network->virtio, &network->receive);
}

static int network_receive(struct netdev *netdev, void *frame, size_t size, size_t *length)
{
	struct virtio_network *network = netdev->context;
	uint32_t written;
	uint16_t id;
	int err = virtqueue_collect(network->virtio, &network->receive, &id, &written);

	if (err != 0) {
		return err;
	}

	if (written < network->header || written - network->header > size) {
		err = -EIO;
	} else {
		*length = written - network->header;
		memcpy(frame, receive_buffer(network, id) + network->header, *length);
	}

	repost(network, id);

	return err;
}

static bool network_link_up(struct netdev *netdev)
{
	struct virtio_network *network = netdev->context;
	uint32_t word;

	if ((network->virtio->features & NET_F_STATUS) == 0) {
		return true;
	}

	word = virtio_config_read32(network->virtio, NET_CONFIG_STATUS);

	return ((word >> NET_STATUS_SHIFT) & NET_STATUS_LINK_UP) != 0;
}

static const struct netdev_ops network_ops = {
	.send		= network_send,
	.receive	= network_receive,
	.link_up	= network_link_up,
};

static void read_mac(struct virtio_network *network)
{
	struct virtio_device *virtio = network->virtio;
	uint8_t *mac = network->netdev.mac;
	uint32_t high;

	if ((virtio->features & NET_F_MAC) == 0) {
		memset(mac, 0, NET_MAC_SIZE);
		mac[0] = LOCAL_MAC_PREFIX;
		mac[NET_MAC_SIZE - 1] = (uint8_t)++generated_macs;
		return;
	}

	put_le32(mac, virtio_config_read32(virtio, NET_CONFIG_MAC));
	high = virtio_config_read32(virtio, NET_CONFIG_MAC + 4);
	mac[4] = (uint8_t)high;
	mac[5] = (uint8_t)(high >> 8);
}

static void release(struct virtio_network *network)
{
	free(network->receive.memory);
	free(network->transmit.memory);
	free(network->buffers);
	free(network->staging);
	free(network);
}

static int network_probe(struct virtio_device *virtio)
{
	struct virtio_network *network;
	int err;

	if (virtio->legacy && (virtio->features & VIRTIO_F_ANY_LAYOUT) == 0) {
		return -ENOTSUP;
	}

	network = calloc(1, sizeof(*network));
	if (network == NULL) {
		return -ENOMEM;
	}

	network->virtio = virtio;
	network->header = virtio->legacy ? HEADER_LEGACY : HEADER_MODERN;
	network->buffers = aligned_alloc(BUFFER_ALIGN, VIRTQ_SIZE_MAX * BUFFER_SIZE);
	network->staging = aligned_alloc(BUFFER_ALIGN, BUFFER_SIZE);

	err = network->buffers != NULL && network->staging != NULL ? 0 : -ENOMEM;
	if (err == 0) {
		err = virtqueue_setup(virtio, &network->receive, RECEIVE_QUEUE);
	}

	if (err == 0) {
		err = virtqueue_setup(virtio, &network->transmit, TRANSMIT_QUEUE);
	}

	if (err != 0) {
		release(network);
		return err;
	}

	read_mac(network);
	network->netdev.ops = &network_ops;
	network->netdev.context = network;
	virtio->context = network;

	return 0;
}

static void network_start(struct virtio_device *virtio)
{
	struct virtio_network *network = virtio->context;
	char mac[NET_MAC_TEXT];
	uint16_t id;
	int err;

	for (id = 0; id < network->receive.size; id++) {
		virtqueue_post(virtio, &network->receive, id, receive_buffer(network, id), BUFFER_SIZE);
	}

	virtqueue_kick(virtio, &network->receive);

	err = net_register(&network->netdev);
	if (err != 0) {
		printf("network %s\n", strerror(-err));
		return;
	}

	net_format_mac(network->netdev.mac, mac);
	printf("network %s %s ok\n", network->netdev.name, mac);
}

static const struct virtio_driver network_driver = {
	.device_id	= VIRTIO_DEVICE_NET,
	.features	= NET_F_MAC | NET_F_STATUS | VIRTIO_F_ANY_LAYOUT,
	.probe		= network_probe,
	.start		= network_start,
};

VIRTIO_DRIVER(network_driver);
