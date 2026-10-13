#ifndef NET_PRIVATE_H
#define NET_PRIVATE_H

#include <lib/net.h>
#include <stdint.h>

#define NET_SECOND_US		1000000ULL

#define ETHER_HEADER_SIZE	14
#define ETHER_DESTINATION	0
#define ETHER_SOURCE		6
#define ETHER_TYPE		12
#define ETHER_TYPE_IPV4		0x0800
#define ETHER_TYPE_ARP		0x0806

#define IPV4_HEADER_SIZE	20
#define IPV4_PROTOCOL_ICMP	1
#define IPV4_PROTOCOL_UDP	17
#define IPV4_ANY		0x00000000U
#define IPV4_BROADCAST		0xffffffffU
#define IPV4_PAYLOAD		(ETHER_HEADER_SIZE + IPV4_HEADER_SIZE)
#define IPV4_PAYLOAD_MAX	(NET_FRAME_MAX - IPV4_PAYLOAD)

#define UDP_HEADER_SIZE		8
#define UDP_PAYLOAD		(IPV4_PAYLOAD + UDP_HEADER_SIZE)
#define UDP_PAYLOAD_MAX		(IPV4_PAYLOAD_MAX - UDP_HEADER_SIZE)

struct ipv4_packet {
	struct netdev	*dev;
	uint8_t		*frame;
	uint32_t	source;
	uint32_t	destination;
	uint8_t		protocol;
	uint8_t		*payload;
	size_t		length;
};

typedef void (*udp_handler)(void *context, const struct ipv4_packet *packet, uint16_t source_port,
			    const uint8_t *data, size_t length);

extern const uint8_t ether_broadcast[NET_MAC_SIZE];

uint32_t net_checksum_add(uint32_t sum, const void *data, size_t length);
uint16_t net_checksum_finish(uint32_t sum);
uint32_t net_random(void);
bool net_local(const struct netdev *dev, uint32_t address);

int ether_send(struct netdev *dev, const uint8_t *destination, uint16_t type, uint8_t *frame, size_t length);
void ether_receive(struct netdev *dev, uint8_t *frame, size_t length);

void arp_receive(struct netdev *dev, const uint8_t *packet, size_t length);
int arp_resolve(struct netdev *dev, uint32_t address, uint8_t *mac);
void arp_forget(const struct netdev *dev);

void ipv4_receive(struct netdev *dev, uint8_t *frame, size_t length);
int ipv4_send(struct netdev *dev, uint32_t destination, uint8_t protocol, uint8_t *frame, size_t length);
int ipv4_reply(const struct ipv4_packet *packet, size_t length);

void icmp_receive(const struct ipv4_packet *packet);

void udp_receive(const struct ipv4_packet *packet);
int udp_bind(uint16_t port, udp_handler handler, void *context);
void udp_unbind(uint16_t port);
int udp_send(struct netdev *dev, uint32_t destination, uint16_t source_port, uint16_t destination_port,
	     const void *data, size_t length);

#endif
