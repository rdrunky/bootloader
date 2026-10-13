#include <errno.h>
#include <string.h>
#include <sys/endian.h>
#include <sys/time.h>

#include "private.h"

#define ARP_ENTRIES		16
#define ARP_LIFETIME_US		(300 * NET_SECOND_US)
#define ARP_ATTEMPTS		3
#define ARP_WAIT_US		(NET_SECOND_US / 2)
#define ARP_FRAME_SIZE		60

#define ARP_HARDWARE		0
#define ARP_PROTOCOL		2
#define ARP_HARDWARE_SIZE	4
#define ARP_PROTOCOL_SIZE	5
#define ARP_OPERATION		6
#define ARP_SENDER_MAC		8
#define ARP_SENDER_ADDRESS	14
#define ARP_TARGET_MAC		18
#define ARP_TARGET_ADDRESS	24
#define ARP_PACKET_SIZE		28

#define ARP_HARDWARE_ETHER	1
#define ARP_REQUEST		1
#define ARP_REPLY		2

struct arp_entry {
	const struct netdev	*dev;
	uint32_t		address;
	uint8_t			mac[NET_MAC_SIZE];
	uint64_t		updated;
};

static const uint8_t unknown_mac[NET_MAC_SIZE];

static struct arp_entry entries[ARP_ENTRIES];

static struct arp_entry *find(const struct netdev *dev, uint32_t address)
{
	uint64_t now = time_us();
	size_t i;

	for (i = 0; i < ARP_ENTRIES; i++) {
		struct arp_entry *entry = &entries[i];

		if (entry->dev == dev && entry->address == address && now - entry->updated < ARP_LIFETIME_US) {
			return entry;
		}
	}

	return NULL;
}

static void store(const struct netdev *dev, uint32_t address, const uint8_t *mac)
{
	struct arp_entry *slot = &entries[0];
	size_t i;

	for (i = 0; i < ARP_ENTRIES; i++) {
		struct arp_entry *entry = &entries[i];

		if (entry->dev == dev && entry->address == address) {
			slot = entry;
			break;
		}

		if (entry->updated < slot->updated) {
			slot = entry;
		}
	}

	slot->dev = dev;
	slot->address = address;
	memcpy(slot->mac, mac, NET_MAC_SIZE);
	slot->updated = time_us();
}

void arp_forget(const struct netdev *dev)
{
	size_t i;

	for (i = 0; i < ARP_ENTRIES; i++) {
		if (entries[i].dev == dev) {
			memset(&entries[i], 0, sizeof(entries[i]));
		}
	}
}

static int send_packet(struct netdev *dev, uint16_t operation, const uint8_t *destination, const uint8_t *target_mac,
		       uint32_t target_address)
{
	uint8_t frame[ARP_FRAME_SIZE];
	uint8_t *packet = frame + ETHER_HEADER_SIZE;

	put_be16(packet + ARP_HARDWARE, ARP_HARDWARE_ETHER);
	put_be16(packet + ARP_PROTOCOL, ETHER_TYPE_IPV4);
	packet[ARP_HARDWARE_SIZE] = NET_MAC_SIZE;
	packet[ARP_PROTOCOL_SIZE] = sizeof(uint32_t);
	put_be16(packet + ARP_OPERATION, operation);
	memcpy(packet + ARP_SENDER_MAC, dev->mac, NET_MAC_SIZE);
	put_be32(packet + ARP_SENDER_ADDRESS, dev->config.address);
	memcpy(packet + ARP_TARGET_MAC, target_mac, NET_MAC_SIZE);
	put_be32(packet + ARP_TARGET_ADDRESS, target_address);

	return ether_send(dev, destination, ETHER_TYPE_ARP, frame, ARP_PACKET_SIZE);
}

void arp_receive(struct netdev *dev, const uint8_t *packet, size_t length)
{
	uint32_t sender;
	uint32_t target;

	if (length < ARP_PACKET_SIZE || get_be16(packet + ARP_HARDWARE) != ARP_HARDWARE_ETHER ||
	    get_be16(packet + ARP_PROTOCOL) != ETHER_TYPE_IPV4 || packet[ARP_HARDWARE_SIZE] != NET_MAC_SIZE ||
	    packet[ARP_PROTOCOL_SIZE] != sizeof(uint32_t)) {
		dev->stats.dropped++;
		return;
	}

	sender = get_be32(packet + ARP_SENDER_ADDRESS);
	target = get_be32(packet + ARP_TARGET_ADDRESS);
	if (!net_configured(dev) || sender == IPV4_ANY) {
		return;
	}

	if (target == dev->config.address || find(dev, sender) != NULL) {
		store(dev, sender, packet + ARP_SENDER_MAC);
	}

	if (get_be16(packet + ARP_OPERATION) == ARP_REQUEST && target == dev->config.address) {
		send_packet(dev, ARP_REPLY, packet + ARP_SENDER_MAC, packet + ARP_SENDER_MAC, sender);
	}
}

static bool lookup(const struct netdev *dev, uint32_t address, uint8_t *mac)
{
	const struct arp_entry *entry = find(dev, address);

	if (entry == NULL) {
		return false;
	}

	memcpy(mac, entry->mac, NET_MAC_SIZE);

	return true;
}

int arp_resolve(struct netdev *dev, uint32_t address, uint8_t *mac)
{
	unsigned int attempt;

	for (attempt = 0; attempt < ARP_ATTEMPTS; attempt++) {
		uint64_t start = time_us();
		int err;

		if (lookup(dev, address, mac)) {
			return 0;
		}

		err = send_packet(dev, ARP_REQUEST, ether_broadcast, unknown_mac, address);
		if (err != 0) {
			return err;
		}

		while (time_us() - start < ARP_WAIT_US) {
			net_poll(dev);
			if (lookup(dev, address, mac)) {
				return 0;
			}
		}
	}

	return -EHOSTUNREACH;
}
