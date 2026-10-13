#include <errno.h>
#include <string.h>
#include <sys/endian.h>

#include "private.h"

#define UDP_SOURCE_PORT		0
#define UDP_DESTINATION_PORT	2
#define UDP_LENGTH		4
#define UDP_CHECKSUM		6
#define UDP_CHECKSUM_NONE	0
#define UDP_CHECKSUM_ZERO	0xffff
#define UDP_BINDINGS		8
#define PSEUDO_HEADER_SIZE	12

struct binding {
	uint16_t	port;
	udp_handler	handler;
	void		*context;
};

static struct binding bindings[UDP_BINDINGS];

static uint16_t checksum(uint32_t source, uint32_t destination, const uint8_t *datagram, size_t length)
{
	uint8_t pseudo[PSEUDO_HEADER_SIZE];

	put_be32(pseudo, source);
	put_be32(pseudo + 4, destination);
	pseudo[8] = 0;
	pseudo[9] = IPV4_PROTOCOL_UDP;
	put_be16(pseudo + 10, (uint16_t)length);

	return net_checksum_finish(net_checksum_add(net_checksum_add(0, pseudo, sizeof(pseudo)), datagram, length));
}

void udp_receive(const struct ipv4_packet *packet)
{
	const uint8_t *datagram = packet->payload;
	size_t length = packet->length >= UDP_HEADER_SIZE ? get_be16(datagram + UDP_LENGTH) : 0;
	uint16_t port;
	size_t i;

	if (length < UDP_HEADER_SIZE || length > packet->length ||
	    (get_be16(datagram + UDP_CHECKSUM) != UDP_CHECKSUM_NONE &&
	     checksum(packet->source, packet->destination, datagram, length) != 0)) {
		packet->dev->stats.dropped++;
		return;
	}

	port = get_be16(datagram + UDP_DESTINATION_PORT);
	for (i = 0; i < UDP_BINDINGS; i++) {
		const struct binding *binding = &bindings[i];

		if (binding->handler != NULL && binding->port == port) {
			binding->handler(binding->context, packet, get_be16(datagram + UDP_SOURCE_PORT),
					 datagram + UDP_HEADER_SIZE, length - UDP_HEADER_SIZE);
			return;
		}
	}

	packet->dev->stats.dropped++;
}

int udp_bind(uint16_t port, udp_handler handler, void *context)
{
	struct binding *slot = NULL;
	size_t i;

	for (i = 0; i < UDP_BINDINGS; i++) {
		if (bindings[i].handler != NULL && bindings[i].port == port) {
			return -EADDRINUSE;
		}

		if (bindings[i].handler == NULL && slot == NULL) {
			slot = &bindings[i];
		}
	}

	if (slot == NULL) {
		return -ENOSPC;
	}

	slot->port = port;
	slot->handler = handler;
	slot->context = context;

	return 0;
}

void udp_unbind(uint16_t port)
{
	size_t i;

	for (i = 0; i < UDP_BINDINGS; i++) {
		if (bindings[i].handler != NULL && bindings[i].port == port) {
			bindings[i].handler = NULL;
		}
	}
}

int udp_send(struct netdev *dev, uint32_t destination, uint16_t source_port, uint16_t destination_port,
	     const void *data, size_t length)
{
	uint8_t *datagram = dev->transmit_frame + IPV4_PAYLOAD;
	uint16_t sum;

	if (length > UDP_PAYLOAD_MAX) {
		return -EMSGSIZE;
	}

	memmove(datagram + UDP_HEADER_SIZE, data, length);
	put_be16(datagram + UDP_SOURCE_PORT, source_port);
	put_be16(datagram + UDP_DESTINATION_PORT, destination_port);
	put_be16(datagram + UDP_LENGTH, (uint16_t)(UDP_HEADER_SIZE + length));
	put_be16(datagram + UDP_CHECKSUM, UDP_CHECKSUM_NONE);

	sum = checksum(dev->config.address, destination, datagram, UDP_HEADER_SIZE + length);
	put_be16(datagram + UDP_CHECKSUM, sum == UDP_CHECKSUM_NONE ? UDP_CHECKSUM_ZERO : sum);

	return ipv4_send(dev, destination, IPV4_PROTOCOL_UDP, dev->transmit_frame, UDP_HEADER_SIZE + length);
}
