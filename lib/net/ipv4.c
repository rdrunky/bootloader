#include <errno.h>
#include <string.h>
#include <sys/endian.h>

#include "private.h"

#define IPV4_VERSION_LENGTH	0
#define IPV4_SERVICE		1
#define IPV4_TOTAL_LENGTH	2
#define IPV4_IDENTIFICATION	4
#define IPV4_FRAGMENT		6
#define IPV4_TTL		8
#define IPV4_PROTOCOL		9
#define IPV4_CHECKSUM		10
#define IPV4_SOURCE		12
#define IPV4_DESTINATION	16

#define IPV4_VERSION		4
#define IPV4_WORDS_MIN		5
#define IPV4_WORD_SIZE		4
#define IPV4_WORDS_MASK		0x0f
#define IPV4_DONT_FRAGMENT	0x4000
#define IPV4_FRAGMENTED		0x3fff
#define IPV4_DEFAULT_TTL	64

static uint16_t identification;

static void write_header(const struct netdev *dev, uint8_t *header, uint32_t destination, uint8_t protocol,
			 size_t length)
{
	header[IPV4_VERSION_LENGTH] = IPV4_VERSION << 4 | IPV4_WORDS_MIN;
	header[IPV4_SERVICE] = 0;
	put_be16(header + IPV4_TOTAL_LENGTH, (uint16_t)(IPV4_HEADER_SIZE + length));
	put_be16(header + IPV4_IDENTIFICATION, identification++);
	put_be16(header + IPV4_FRAGMENT, IPV4_DONT_FRAGMENT);
	header[IPV4_TTL] = IPV4_DEFAULT_TTL;
	header[IPV4_PROTOCOL] = protocol;
	put_be16(header + IPV4_CHECKSUM, 0);
	put_be32(header + IPV4_SOURCE, dev->config.address);
	put_be32(header + IPV4_DESTINATION, destination);
	put_be16(header + IPV4_CHECKSUM, net_checksum_finish(net_checksum_add(0, header, IPV4_HEADER_SIZE)));
}

static uint32_t subnet_broadcast(const struct netdev *dev)
{
	return dev->config.address | ~dev->config.netmask;
}

static int next_hop(struct netdev *dev, uint32_t destination, uint8_t *mac)
{
	if (destination == IPV4_BROADCAST) {
		memcpy(mac, ether_broadcast, NET_MAC_SIZE);
		return 0;
	}

	if (!net_configured(dev)) {
		return -ENETDOWN;
	}

	if (destination == subnet_broadcast(dev)) {
		memcpy(mac, ether_broadcast, NET_MAC_SIZE);
		return 0;
	}

	if (net_local(dev, destination)) {
		return arp_resolve(dev, destination, mac);
	}

	if (dev->config.gateway == IPV4_ANY) {
		return -ENETUNREACH;
	}

	return arp_resolve(dev, dev->config.gateway, mac);
}

int ipv4_send(struct netdev *dev, uint32_t destination, uint8_t protocol, uint8_t *frame, size_t length)
{
	uint8_t mac[NET_MAC_SIZE];
	int err;

	if (length > IPV4_PAYLOAD_MAX) {
		return -EMSGSIZE;
	}

	err = next_hop(dev, destination, mac);
	if (err != 0) {
		return err;
	}

	write_header(dev, frame + ETHER_HEADER_SIZE, destination, protocol, length);

	return ether_send(dev, mac, ETHER_TYPE_IPV4, frame, IPV4_HEADER_SIZE + length);
}

int ipv4_reply(const struct ipv4_packet *packet, size_t length)
{
	uint8_t *payload = packet->frame + IPV4_PAYLOAD;
	uint8_t destination[NET_MAC_SIZE];

	memcpy(destination, packet->frame + ETHER_SOURCE, NET_MAC_SIZE);
	if (packet->payload != payload) {
		memmove(payload, packet->payload, length);
	}

	write_header(packet->dev, packet->frame + ETHER_HEADER_SIZE, packet->source, packet->protocol, length);

	return ether_send(packet->dev, destination, ETHER_TYPE_IPV4, packet->frame, IPV4_HEADER_SIZE + length);
}

static bool accepted(const struct netdev *dev, uint32_t destination)
{
	if (!net_configured(dev)) {
		return true;
	}

	return destination == dev->config.address || destination == IPV4_BROADCAST ||
	       destination == subnet_broadcast(dev);
}

static bool parse(struct netdev *dev, uint8_t *frame, size_t length, struct ipv4_packet *packet)
{
	uint8_t *header = frame + ETHER_HEADER_SIZE;
	size_t available = length - ETHER_HEADER_SIZE;
	size_t header_length;
	size_t total;

	if (available < IPV4_HEADER_SIZE || header[IPV4_VERSION_LENGTH] >> 4 != IPV4_VERSION) {
		return false;
	}

	header_length = (size_t)(header[IPV4_VERSION_LENGTH] & IPV4_WORDS_MASK) * IPV4_WORD_SIZE;
	total = get_be16(header + IPV4_TOTAL_LENGTH);
	if (header_length < IPV4_HEADER_SIZE || total < header_length || total > available ||
	    net_checksum_finish(net_checksum_add(0, header, header_length)) != 0 ||
	    (get_be16(header + IPV4_FRAGMENT) & IPV4_FRAGMENTED) != 0) {
		return false;
	}

	packet->dev = dev;
	packet->frame = frame;
	packet->source = get_be32(header + IPV4_SOURCE);
	packet->destination = get_be32(header + IPV4_DESTINATION);
	packet->protocol = header[IPV4_PROTOCOL];
	packet->payload = header + header_length;
	packet->length = total - header_length;

	return accepted(dev, packet->destination);
}

void ipv4_receive(struct netdev *dev, uint8_t *frame, size_t length)
{
	struct ipv4_packet packet;

	if (!parse(dev, frame, length, &packet)) {
		dev->stats.dropped++;
		return;
	}

	switch (packet.protocol) {
	case IPV4_PROTOCOL_ICMP:
		icmp_receive(&packet);
		break;
	case IPV4_PROTOCOL_UDP:
		udp_receive(&packet);
		break;
	default:
		dev->stats.dropped++;
		break;
	}
}
