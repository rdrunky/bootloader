#include <errno.h>
#include <string.h>
#include <sys/endian.h>

#include "private.h"

#define ETHER_FRAME_MIN		60

const uint8_t ether_broadcast[NET_MAC_SIZE] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

int ether_send(struct netdev *dev, const uint8_t *destination, uint16_t type, uint8_t *frame, size_t length)
{
	size_t total = ETHER_HEADER_SIZE + length;
	int err;

	if (total > NET_FRAME_MAX) {
		return -EMSGSIZE;
	}

	if (!net_link_up(dev)) {
		return -ENETDOWN;
	}

	memcpy(frame + ETHER_DESTINATION, destination, NET_MAC_SIZE);
	memcpy(frame + ETHER_SOURCE, dev->mac, NET_MAC_SIZE);
	put_be16(frame + ETHER_TYPE, type);

	if (total < ETHER_FRAME_MIN) {
		memset(frame + total, 0, ETHER_FRAME_MIN - total);
		total = ETHER_FRAME_MIN;
	}

	err = dev->ops->send(dev, frame, total);
	if (err != 0) {
		dev->stats.errors++;
		return err;
	}

	dev->stats.sent++;

	return 0;
}

static bool addressed_to(const struct netdev *dev, const uint8_t *destination)
{
	return memcmp(destination, dev->mac, NET_MAC_SIZE) == 0 ||
	       memcmp(destination, ether_broadcast, NET_MAC_SIZE) == 0;
}

void ether_receive(struct netdev *dev, uint8_t *frame, size_t length)
{
	if (length < ETHER_HEADER_SIZE || !addressed_to(dev, frame + ETHER_DESTINATION)) {
		dev->stats.dropped++;
		return;
	}

	switch (get_be16(frame + ETHER_TYPE)) {
	case ETHER_TYPE_ARP:
		arp_receive(dev, frame + ETHER_HEADER_SIZE, length - ETHER_HEADER_SIZE);
		break;
	case ETHER_TYPE_IPV4:
		ipv4_receive(dev, frame, length);
		break;
	default:
		dev->stats.dropped++;
		break;
	}
}
