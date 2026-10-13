#include <errno.h>
#include <lib/net.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>
#include <sys/time.h>

#include "harness.h"

#define CLOCK_STEP_US		1000
#define WIRE_FRAMES		16
#define FRAME_MIN		60
#define FUZZ_ROUNDS		20000
#define DHCP_ROUNDS		200
#define PING_TIMEOUT_US		1000000

#define ETHER_HEADER		14
#define ETHER_TYPE_OFFSET	12
#define TYPE_IPV4		0x0800
#define TYPE_ARP		0x0806
#define TYPE_IPV6		0x86dd
#define IP_HEADER		20
#define IP_OPTIONS		4
#define IP_PAYLOAD		(ETHER_HEADER + IP_HEADER)
#define IP_MORE_FRAGMENTS	0x2000
#define UDP_HEADER		8
#define UDP_PAYLOAD		(IP_PAYLOAD + UDP_HEADER)
#define PROTOCOL_ICMP		1
#define PROTOCOL_UDP		17
#define ARP_SIZE		28
#define ARP_REQUEST		1
#define ARP_REPLY		2
#define ICMP_HEADER		8
#define ECHO_REQUEST		8
#define ECHO_REPLY		0

#define DHCP_SERVER_PORT	67
#define DHCP_CLIENT_PORT	68
#define DHCP_MAGIC_OFFSET	236
#define DHCP_FIXED		240
#define DHCP_SIZE		300
#define DHCP_MAGIC		0x63825363U
#define DHCP_DISCOVER		1
#define DHCP_OFFER		2
#define DHCP_REQUEST		3
#define DHCP_ACK		5
#define DHCP_NAK		6
#define OPTION_PAD		0
#define OPTION_NETMASK		1
#define OPTION_ROUTER		3
#define OPTION_DNS		6
#define OPTION_REQUESTED	50
#define OPTION_LEASE		51
#define OPTION_TYPE		53
#define OPTION_SERVER		54
#define OPTION_END		255

#define ADDRESS(a, b, c, d)	((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define GATEWAY			ADDRESS(10, 0, 2, 2)
#define NAMESERVER		ADDRESS(10, 0, 2, 3)
#define CLIENT			ADDRESS(10, 0, 2, 15)
#define STRANGER		ADDRESS(10, 0, 2, 99)
#define REMOTE			ADDRESS(192, 168, 1, 1)
#define BROADCAST		ADDRESS(255, 255, 255, 255)
#define NETMASK			ADDRESS(255, 255, 255, 0)
#define LEASE			86400

enum dhcp_mode {
	DHCP_SERVE,
	DHCP_SILENT,
	DHCP_REFUSE,
	DHCP_LATE,
	DHCP_MANGLE,
};

struct wire {
	struct netdev	dev;
	uint8_t		inbox[WIRE_FRAMES][NET_FRAME_MAX];
	size_t		inbox_lengths[WIRE_FRAMES];
	unsigned int	inbox_head;
	unsigned int	inbox_count;
	uint8_t		*guarded;
	uint8_t		sent[NET_FRAME_MAX];
	size_t		sent_length;
	unsigned int	frames_sent;
	unsigned int	malformed;
	unsigned int	arp_requests;
	unsigned int	echo_requests;
	unsigned int	discovers;
	unsigned int	requests;
	uint32_t	requested_address;
	uint32_t	requested_server;
	bool		answers_arp;
	bool		answers_echo;
	enum dhcp_mode	dhcp;
	uint64_t	random;
};

static const uint8_t client_mac[NET_MAC_SIZE] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static const uint8_t gateway_mac[NET_MAC_SIZE] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };
static const uint8_t broadcast_mac[NET_MAC_SIZE] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static const uint8_t zero_mac[NET_MAC_SIZE];
static const uint8_t echo_message[] = { ECHO_REQUEST, 0, 0, 0, 0x12, 0x34, 0x00, 0x07, 'h', 'e', 'l', 'l', 'o' };

static uint64_t clock_us = 1;

uint64_t time_us(void)
{
	clock_us += CLOCK_STEP_US;

	return clock_us;
}

static uint16_t oracle_checksum(const uint8_t *data, size_t length, uint32_t sum)
{
	size_t i;

	for (i = 0; i + 1 < length; i += 2) {
		sum += (uint32_t)data[i] << 8 | data[i + 1];
	}

	if (length % 2 != 0) {
		sum += (uint32_t)data[length - 1] << 8;
	}

	while (sum > 0xffff) {
		sum = (sum & 0xffff) + (sum >> 16);
	}

	return (uint16_t)~sum;
}

static uint32_t pseudo_sum(uint32_t source, uint32_t destination, size_t length)
{
	return (source >> 16) + (source & 0xffff) + (destination >> 16) + (destination & 0xffff) + PROTOCOL_UDP +
	       (uint32_t)length;
}

static void put_ether(uint8_t *frame, const uint8_t *destination, uint16_t type)
{
	memcpy(frame, destination, NET_MAC_SIZE);
	memcpy(frame + NET_MAC_SIZE, gateway_mac, NET_MAC_SIZE);
	put_be16(frame + ETHER_TYPE_OFFSET, type);
}

static void put_ipv4(uint8_t *frame, uint32_t source, uint32_t destination, uint8_t protocol, size_t payload)
{
	uint8_t *ip = frame + ETHER_HEADER;

	memset(ip, 0, IP_HEADER);
	ip[0] = 0x45;
	put_be16(ip + 2, (uint16_t)(IP_HEADER + payload));
	ip[8] = 64;
	ip[9] = protocol;
	put_be32(ip + 12, source);
	put_be32(ip + 16, destination);
	put_be16(ip + 10, oracle_checksum(ip, IP_HEADER, 0));
}

static size_t build_arp(uint8_t *frame, uint16_t operation, const uint8_t *destination, uint32_t sender,
			const uint8_t *target_mac, uint32_t target)
{
	uint8_t *arp = frame + ETHER_HEADER;

	memset(frame, 0, FRAME_MIN);
	put_ether(frame, destination, TYPE_ARP);
	put_be16(arp, 1);
	put_be16(arp + 2, TYPE_IPV4);
	arp[4] = NET_MAC_SIZE;
	arp[5] = sizeof(uint32_t);
	put_be16(arp + 6, operation);
	memcpy(arp + 8, gateway_mac, NET_MAC_SIZE);
	put_be32(arp + 14, sender);
	memcpy(arp + 18, target_mac, NET_MAC_SIZE);
	put_be32(arp + 24, target);

	return ETHER_HEADER + ARP_SIZE;
}

static size_t build_icmp(uint8_t *frame, const uint8_t *destination_mac, uint32_t source, uint32_t destination,
			 const uint8_t *message, size_t length)
{
	uint8_t *icmp = frame + IP_PAYLOAD;

	put_ether(frame, destination_mac, TYPE_IPV4);
	put_ipv4(frame, source, destination, PROTOCOL_ICMP, length);
	memcpy(icmp, message, length);
	put_be16(icmp + 2, 0);
	put_be16(icmp + 2, oracle_checksum(icmp, length, 0));

	return IP_PAYLOAD + length;
}

static size_t build_icmp_with_options(uint8_t *frame, const uint8_t *message, size_t length)
{
	uint8_t *ip = frame + ETHER_HEADER;
	uint8_t *icmp = ip + IP_HEADER + IP_OPTIONS;

	put_ether(frame, client_mac, TYPE_IPV4);
	memset(ip, 0, IP_HEADER + IP_OPTIONS);
	ip[0] = 0x46;
	put_be16(ip + 2, (uint16_t)(IP_HEADER + IP_OPTIONS + length));
	ip[8] = 64;
	ip[9] = PROTOCOL_ICMP;
	put_be32(ip + 12, GATEWAY);
	put_be32(ip + 16, CLIENT);
	memset(ip + IP_HEADER, 1, IP_OPTIONS);
	put_be16(ip + 10, oracle_checksum(ip, IP_HEADER + IP_OPTIONS, 0));
	memcpy(icmp, message, length);
	put_be16(icmp + 2, 0);
	put_be16(icmp + 2, oracle_checksum(icmp, length, 0));

	return ETHER_HEADER + IP_HEADER + IP_OPTIONS + length;
}

static size_t build_udp(uint8_t *frame, const uint8_t *destination_mac, uint32_t source, uint32_t destination,
			uint16_t destination_port, size_t length)
{
	uint8_t *udp = frame + IP_PAYLOAD;

	put_ether(frame, destination_mac, TYPE_IPV4);
	put_ipv4(frame, source, destination, PROTOCOL_UDP, UDP_HEADER + length);
	put_be16(udp, DHCP_SERVER_PORT);
	put_be16(udp + 2, destination_port);
	put_be16(udp + 4, (uint16_t)(UDP_HEADER + length));
	put_be16(udp + 6, 0);
	put_be16(udp + 6, oracle_checksum(udp, UDP_HEADER + length, pseudo_sum(source, destination, UDP_HEADER + length)));

	return UDP_PAYLOAD + length;
}

static void deliver(struct wire *wire, const uint8_t *frame, size_t length)
{
	unsigned int slot = (wire->inbox_head + wire->inbox_count) % WIRE_FRAMES;

	if (wire->inbox_count < WIRE_FRAMES && length <= NET_FRAME_MAX) {
		memcpy(wire->inbox[slot], frame, length);
		wire->inbox_lengths[slot] = length;
		wire->inbox_count++;
	}
}

static size_t mangle(struct wire *wire, uint8_t *data, size_t length, size_t focus)
{
	unsigned int flips = 1 + test_random(&wire->random) % 6;
	unsigned int i;

	for (i = 0; i < flips && length > 0; i++) {
		size_t at = focus < length && test_random(&wire->random) % 2 == 0 ? focus + test_random(&wire->random) % (length - focus)
									 : test_random(&wire->random) % length;

		data[at] ^= (uint8_t)(1 + test_random(&wire->random) % 255);
	}

	switch (test_random(&wire->random) % 8) {
	case 0:
		return length > 0 ? test_random(&wire->random) % length : 0;
	case 1:
		return length + test_random(&wire->random) % (NET_FRAME_MAX - length + 1);
	default:
		return length;
	}
}

static bool udp_well_formed(const uint8_t *ip, size_t total)
{
	const uint8_t *udp = ip + IP_HEADER;
	size_t length = total - IP_HEADER;

	return length >= UDP_HEADER && get_be16(udp + 4) == length &&
	       (get_be16(udp + 6) == 0 ||
		oracle_checksum(udp, length, pseudo_sum(get_be32(ip + 12), get_be32(ip + 16), length)) == 0);
}

static bool well_formed(const uint8_t *frame, size_t length)
{
	const uint8_t *ip = frame + ETHER_HEADER;
	size_t total;

	if (length < FRAME_MIN || length > NET_FRAME_MAX || memcmp(frame + NET_MAC_SIZE, client_mac, NET_MAC_SIZE) != 0) {
		return false;
	}

	if (get_be16(frame + ETHER_TYPE_OFFSET) == TYPE_ARP) {
		return get_be16(ip) == 1 && get_be16(ip + 2) == TYPE_IPV4 && ip[4] == NET_MAC_SIZE &&
		       ip[5] == sizeof(uint32_t) && memcmp(ip + 8, client_mac, NET_MAC_SIZE) == 0;
	}

	total = get_be16(ip + 2);
	if (get_be16(frame + ETHER_TYPE_OFFSET) != TYPE_IPV4 || ip[0] != 0x45 || total < IP_HEADER ||
	    ETHER_HEADER + total > length || oracle_checksum(ip, IP_HEADER, 0) != 0) {
		return false;
	}

	if (ip[9] == PROTOCOL_ICMP) {
		return total >= IP_HEADER + ICMP_HEADER && oracle_checksum(ip + IP_HEADER, total - IP_HEADER, 0) == 0;
	}

	return ip[9] == PROTOCOL_UDP && udp_well_formed(ip, total);
}

static void answer_arp(struct wire *wire, const uint8_t *frame)
{
	const uint8_t *arp = frame + ETHER_HEADER;
	uint8_t reply[NET_FRAME_MAX];

	if (get_be16(arp + 6) != ARP_REQUEST) {
		return;
	}

	wire->arp_requests++;
	if (wire->answers_arp && get_be32(arp + 24) == GATEWAY) {
		deliver(wire, reply, build_arp(reply, ARP_REPLY, arp + 8, GATEWAY, arp + 8, get_be32(arp + 14)));
	}
}

static void answer_echo(struct wire *wire, const uint8_t *frame)
{
	const uint8_t *ip = frame + ETHER_HEADER;
	const uint8_t *icmp = frame + IP_PAYLOAD;
	size_t length = get_be16(ip + 2) - IP_HEADER;
	uint8_t message[NET_FRAME_MAX];
	uint8_t reply[NET_FRAME_MAX];

	if (icmp[0] != ECHO_REQUEST) {
		return;
	}

	wire->echo_requests++;
	if (wire->answers_echo && get_be32(ip + 16) == GATEWAY) {
		memcpy(message, icmp, length);
		message[0] = ECHO_REPLY;
		deliver(wire, reply, build_icmp(reply, frame + NET_MAC_SIZE, GATEWAY, get_be32(ip + 12), message, length));
	}
}

static const uint8_t *find_option(const uint8_t *message, size_t length, uint8_t code)
{
	size_t at = DHCP_FIXED;

	while (at + 1 < length && message[at] != OPTION_END) {
		if (message[at] == OPTION_PAD) {
			at++;
			continue;
		}

		if (message[at] == code && at + 2 + message[at + 1] <= length) {
			return message + at + 1;
		}

		at += 2 + (size_t)message[at + 1];
	}

	return NULL;
}

static uint32_t option_address(const uint8_t *message, size_t length, uint8_t code)
{
	const uint8_t *option = find_option(message, length, code);

	return option != NULL && option[0] == sizeof(uint32_t) ? get_be32(option + 1) : 0;
}

static size_t add_option(uint8_t *message, size_t at, uint8_t code, uint32_t value, uint8_t length)
{
	message[at] = code;
	message[at + 1] = length;
	if (length == 1) {
		message[at + 2] = (uint8_t)value;
	} else {
		put_be32(message + at + 2, value);
	}

	return at + 2 + length;
}

static void dhcp_reply(struct wire *wire, const uint8_t *request, uint8_t type)
{
	uint8_t frame[NET_FRAME_MAX];
	uint8_t *message = frame + UDP_PAYLOAD;
	size_t at = DHCP_FIXED;
	size_t length;

	memset(frame, 0, sizeof(frame));
	message[0] = 2;
	message[1] = 1;
	message[2] = NET_MAC_SIZE;
	memcpy(message + 4, request + 4, sizeof(uint32_t));
	put_be32(message + 16, type == DHCP_NAK ? 0 : CLIENT);
	put_be32(message + 20, GATEWAY);
	memcpy(message + 28, request + 28, NET_MAC_SIZE);
	put_be32(message + DHCP_MAGIC_OFFSET, DHCP_MAGIC);

	at = add_option(message, at, OPTION_TYPE, type, 1);
	at = add_option(message, at, OPTION_SERVER, GATEWAY, sizeof(uint32_t));
	if (type != DHCP_NAK) {
		at = add_option(message, at, OPTION_NETMASK, NETMASK, sizeof(uint32_t));
		at = add_option(message, at, OPTION_ROUTER, GATEWAY, sizeof(uint32_t));
		at = add_option(message, at, OPTION_DNS, NAMESERVER, sizeof(uint32_t));
		at = add_option(message, at, OPTION_LEASE, LEASE, sizeof(uint32_t));
	}
	message[at] = OPTION_END;

	length = DHCP_SIZE;
	if (wire->dhcp == DHCP_MANGLE) {
		length = mangle(wire, message, length, DHCP_MAGIC_OFFSET);
		if (length > NET_FRAME_MAX - UDP_PAYLOAD) {
			length = NET_FRAME_MAX - UDP_PAYLOAD;
		}
	}

	deliver(wire, frame, build_udp(frame, broadcast_mac, GATEWAY, BROADCAST, DHCP_CLIENT_PORT, length));
}

static void answer_dhcp(struct wire *wire, const uint8_t *frame)
{
	const uint8_t *message = frame + UDP_PAYLOAD;
	size_t length = get_be16(frame + IP_PAYLOAD + 4) - UDP_HEADER;
	const uint8_t *type = find_option(message, length, OPTION_TYPE);

	if (type == NULL || type[0] != 1) {
		return;
	}

	if (type[1] == DHCP_DISCOVER) {
		wire->discovers++;
		if (wire->dhcp != DHCP_SILENT && !(wire->dhcp == DHCP_LATE && wire->discovers == 1)) {
			dhcp_reply(wire, message, DHCP_OFFER);
		}
	} else if (type[1] == DHCP_REQUEST) {
		wire->requests++;
		wire->requested_address = option_address(message, length, OPTION_REQUESTED);
		wire->requested_server = option_address(message, length, OPTION_SERVER);
		dhcp_reply(wire, message, wire->dhcp == DHCP_REFUSE ? DHCP_NAK : DHCP_ACK);
	}
}

static void peer_react(struct wire *wire, const uint8_t *frame)
{
	const uint8_t *ip = frame + ETHER_HEADER;

	if (get_be16(frame + ETHER_TYPE_OFFSET) == TYPE_ARP) {
		answer_arp(wire, frame);
	} else if (ip[9] == PROTOCOL_ICMP) {
		answer_echo(wire, frame);
	} else if (ip[9] == PROTOCOL_UDP && get_be16(frame + IP_PAYLOAD + 2) == DHCP_SERVER_PORT) {
		answer_dhcp(wire, frame);
	}
}

static int wire_send(struct netdev *dev, const void *frame, size_t length)
{
	struct wire *wire = dev->context;

	if (!well_formed(frame, length)) {
		wire->malformed++;
		return -EINVAL;
	}

	memcpy(wire->sent, frame, length);
	wire->sent_length = length;
	wire->frames_sent++;
	peer_react(wire, wire->sent);

	return 0;
}

static int wire_receive(struct netdev *dev, void *frame, size_t size, size_t *length)
{
	struct wire *wire = dev->context;
	unsigned int slot = wire->inbox_head;
	size_t received = wire->inbox_lengths[slot];

	if (wire->inbox_count == 0) {
		return -EAGAIN;
	}

	wire->inbox_head = (slot + 1) % WIRE_FRAMES;
	wire->inbox_count--;

	if (received > size) {
		return -EIO;
	}

	if (wire->guarded != NULL) {
		dev->receive_frame = wire->guarded + NET_FRAME_MAX - (received < FRAME_MIN ? FRAME_MIN : received);
		frame = dev->receive_frame;
	}

	memcpy(frame, wire->inbox[slot], received);
	*length = received;

	return 0;
}

static const struct netdev_ops wire_ops = {
	.send		= wire_send,
	.receive	= wire_receive,
};

static struct wire *wire_create(void)
{
	struct wire *wire = calloc(1, sizeof(*wire));

	if (wire == NULL) {
		return NULL;
	}

	memcpy(wire->dev.mac, client_mac, NET_MAC_SIZE);
	wire->dev.ops = &wire_ops;
	wire->dev.context = wire;
	wire->answers_arp = true;
	wire->answers_echo = true;
	wire->dhcp = DHCP_SERVE;
	wire->random = 0x2545f491;

	if (net_register(&wire->dev) != 0) {
		free(wire);
		return NULL;
	}

	return wire;
}

static struct wire *configured_wire(void)
{
	struct wire *wire = wire_create();

	if (wire != NULL) {
		net_configure(&wire->dev, CLIENT, NETMASK, GATEWAY);
	}

	return wire;
}

static bool valid_netmask(uint32_t netmask)
{
	uint32_t hosts = ~netmask;

	return netmask != 0 && (hosts & (hosts + 1)) == 0;
}

TEST(net, parses_and_formats_addresses)
{
	char text[NET_ADDRESS_TEXT];
	char mac[NET_MAC_TEXT];
	uint32_t address;
	uint32_t netmask;

	CHECK_EQ(net_parse_address("10.0.2.15", &address), 0);
	CHECK_EQ(address, CLIENT);
	net_format_address(address, text);
	CHECK_STR(text, "10.0.2.15");
	CHECK_EQ(net_parse_address("255.255.255.255", &address), 0);
	CHECK_EQ(address, 0xffffffffU);
	CHECK_EQ(net_parse_address("0.0.0.0", &address), 0);
	CHECK_EQ(address, 0);

	CHECK_EQ(net_parse_address("256.1.1.1", &address), -EINVAL);
	CHECK_EQ(net_parse_address("1.2.3", &address), -EINVAL);
	CHECK_EQ(net_parse_address("1.2.3.4.5", &address), -EINVAL);
	CHECK_EQ(net_parse_address("01.2.3.4", &address), -EINVAL);
	CHECK_EQ(net_parse_address("1..3.4", &address), -EINVAL);
	CHECK_EQ(net_parse_address("1.2.3.4 ", &address), -EINVAL);
	CHECK_EQ(net_parse_address("1234.1.1.1", &address), -EINVAL);
	CHECK_EQ(net_parse_address("", &address), -EINVAL);

	CHECK_EQ(net_parse_prefix("10.0.2.15/24", &address, &netmask), 0);
	CHECK_EQ(address, CLIENT);
	CHECK_EQ(netmask, NETMASK);
	CHECK_EQ(net_prefix_length(netmask), 24);
	CHECK_EQ(net_parse_prefix("10.0.2.15/32", &address, &netmask), 0);
	CHECK_EQ(netmask, 0xffffffffU);
	CHECK_EQ(net_prefix_length(netmask), 32);
	CHECK_EQ(net_parse_prefix("10.0.2.15/0", &address, &netmask), 0);
	CHECK_EQ(netmask, 0);
	CHECK_EQ(net_parse_prefix("10.0.2.15/33", &address, &netmask), -EINVAL);
	CHECK_EQ(net_parse_prefix("10.0.2.15", &address, &netmask), -EINVAL);
	CHECK_EQ(net_parse_prefix("10.0.2.15/", &address, &netmask), -EINVAL);
	CHECK_EQ(net_parse_prefix("10.0.2.15/2x", &address, &netmask), -EINVAL);
	CHECK_EQ(net_parse_prefix("10.0.2.15/240", &address, &netmask), -EINVAL);

	net_format_mac(client_mac, mac);
	CHECK_STR(mac, "52:54:00:12:34:56");
}

TEST(net, configures_with_dhcp)
{
	struct wire *wire = wire_create();
	uint64_t elapsed;

	CHECK(wire != NULL);
	CHECK(!net_configured(&wire->dev));
	CHECK_EQ(icmp_ping(&wire->dev, GATEWAY, 1, PING_TIMEOUT_US, &elapsed), -ENETDOWN);

	CHECK_EQ(dhcp_configure(&wire->dev), 0);
	CHECK_EQ(wire->dev.config.address, CLIENT);
	CHECK_EQ(wire->dev.config.netmask, NETMASK);
	CHECK_EQ(wire->dev.config.gateway, GATEWAY);
	CHECK_EQ(wire->dev.config.dns, NAMESERVER);
	CHECK_EQ(wire->dev.config.server, GATEWAY);
	CHECK_EQ(wire->dev.config.lease, LEASE);
	CHECK(wire->dev.config.dynamic);
	CHECK_EQ(wire->discovers, 1);
	CHECK_EQ(wire->requests, 1);
	CHECK_EQ(wire->requested_address, CLIENT);
	CHECK_EQ(wire->requested_server, GATEWAY);
	CHECK_EQ(get_be32(wire->sent + ETHER_HEADER + 12), 0);
	CHECK_EQ(get_be32(wire->sent + ETHER_HEADER + 16), 0xffffffffU);
	CHECK(memcmp(wire->sent, broadcast_mac, NET_MAC_SIZE) == 0);
	CHECK_EQ(wire->malformed, 0);
	CHECK(net_route(GATEWAY) != NULL);
}

TEST(net, retransmits_an_unanswered_discover)
{
	struct wire *wire = wire_create();

	CHECK(wire != NULL);
	wire->dhcp = DHCP_LATE;
	CHECK_EQ(dhcp_configure(&wire->dev), 0);
	CHECK_EQ(wire->discovers, 2);
	CHECK_EQ(wire->dev.config.address, CLIENT);
}

TEST(net, reports_a_refused_request)
{
	struct wire *wire = wire_create();

	CHECK(wire != NULL);
	wire->dhcp = DHCP_REFUSE;
	CHECK_EQ(dhcp_configure(&wire->dev), -ECONNREFUSED);
	CHECK(!net_configured(&wire->dev));
}

TEST(net, gives_up_without_a_dhcp_server)
{
	struct wire *wire = configured_wire();

	CHECK(wire != NULL);
	wire->dhcp = DHCP_SILENT;
	CHECK_EQ(dhcp_configure(&wire->dev), -ETIMEDOUT);
	CHECK_EQ(wire->discovers, 3);
	CHECK(!net_configured(&wire->dev));
	CHECK_EQ(wire->malformed, 0);
}

TEST(net, resolves_the_gateway_once_and_routes_through_it)
{
	struct wire *wire = configured_wire();
	uint64_t elapsed;

	CHECK(wire != NULL);
	CHECK_EQ(icmp_ping(&wire->dev, GATEWAY, 1, PING_TIMEOUT_US, &elapsed), 0);
	CHECK_EQ(icmp_ping(&wire->dev, GATEWAY, 2, PING_TIMEOUT_US, &elapsed), 0);
	CHECK_EQ(wire->arp_requests, 1);
	CHECK_EQ(wire->echo_requests, 2);

	CHECK_EQ(icmp_ping(&wire->dev, REMOTE, 3, PING_TIMEOUT_US, &elapsed), -ETIMEDOUT);
	CHECK(memcmp(wire->sent, gateway_mac, NET_MAC_SIZE) == 0);
	CHECK_EQ(get_be32(wire->sent + ETHER_HEADER + 16), REMOTE);
	CHECK_EQ(wire->arp_requests, 1);

	wire->answers_arp = false;
	CHECK_EQ(icmp_ping(&wire->dev, STRANGER, 4, PING_TIMEOUT_US, &elapsed), -EHOSTUNREACH);
	CHECK_EQ(wire->arp_requests, 4);
	CHECK_EQ(wire->malformed, 0);
}

TEST(net, answers_arp_requests_for_its_address)
{
	struct wire *wire = configured_wire();
	uint8_t frame[NET_FRAME_MAX];
	const uint8_t *arp;

	CHECK(wire != NULL);
	deliver(wire, frame, build_arp(frame, ARP_REQUEST, broadcast_mac, GATEWAY, zero_mac, STRANGER));
	net_poll(&wire->dev);
	CHECK_EQ(wire->frames_sent, 0);

	deliver(wire, frame, build_arp(frame, ARP_REQUEST, broadcast_mac, GATEWAY, zero_mac, CLIENT));
	net_poll(&wire->dev);
	CHECK_EQ(wire->frames_sent, 1);

	arp = wire->sent + ETHER_HEADER;
	CHECK(memcmp(wire->sent, gateway_mac, NET_MAC_SIZE) == 0);
	CHECK_EQ(get_be16(arp + 6), ARP_REPLY);
	CHECK(memcmp(arp + 8, client_mac, NET_MAC_SIZE) == 0);
	CHECK_EQ(get_be32(arp + 14), CLIENT);
	CHECK(memcmp(arp + 18, gateway_mac, NET_MAC_SIZE) == 0);
	CHECK_EQ(get_be32(arp + 24), GATEWAY);
	CHECK_EQ(wire->malformed, 0);
}

TEST(net, answers_echo_requests)
{
	struct wire *wire = configured_wire();
	uint8_t frame[NET_FRAME_MAX];
	const uint8_t *icmp;

	CHECK(wire != NULL);
	deliver(wire, frame, build_icmp(frame, client_mac, GATEWAY, CLIENT, echo_message, sizeof(echo_message)));
	net_poll(&wire->dev);
	CHECK_EQ(wire->frames_sent, 1);

	icmp = wire->sent + IP_PAYLOAD;
	CHECK(memcmp(wire->sent, gateway_mac, NET_MAC_SIZE) == 0);
	CHECK_EQ(get_be32(wire->sent + ETHER_HEADER + 12), CLIENT);
	CHECK_EQ(get_be32(wire->sent + ETHER_HEADER + 16), GATEWAY);
	CHECK_EQ(icmp[0], ECHO_REPLY);
	CHECK(memcmp(icmp + 4, echo_message + 4, sizeof(echo_message) - 4) == 0);

	deliver(wire, frame, build_icmp_with_options(frame, echo_message, sizeof(echo_message)));
	net_poll(&wire->dev);
	CHECK_EQ(wire->frames_sent, 2);
	CHECK_EQ(wire->sent[ETHER_HEADER], 0x45);
	CHECK_EQ(icmp[0], ECHO_REPLY);
	CHECK(memcmp(icmp + 4, echo_message + 4, sizeof(echo_message) - 4) == 0);

	deliver(wire, frame, build_icmp(frame, broadcast_mac, GATEWAY, ADDRESS(10, 0, 2, 255), echo_message,
					sizeof(echo_message)));
	net_poll(&wire->dev);
	CHECK_EQ(wire->frames_sent, 2);
	CHECK_EQ(wire->malformed, 0);
}

TEST(net, drops_malformed_frames)
{
	struct wire *wire = configured_wire();
	uint8_t frame[NET_FRAME_MAX];
	uint64_t dropped;
	size_t length;

	CHECK(wire != NULL);
	dropped = wire->dev.stats.dropped;

	length = build_icmp(frame, client_mac, GATEWAY, CLIENT, echo_message, sizeof(echo_message));
	frame[ETHER_HEADER + 10] ^= 1;
	deliver(wire, frame, length);

	length = build_icmp(frame, client_mac, GATEWAY, CLIENT, echo_message, sizeof(echo_message));
	put_be16(frame + ETHER_HEADER + 6, IP_MORE_FRAGMENTS);
	put_be16(frame + ETHER_HEADER + 10, 0);
	put_be16(frame + ETHER_HEADER + 10, oracle_checksum(frame + ETHER_HEADER, IP_HEADER, 0));
	deliver(wire, frame, length);

	length = build_udp(frame, client_mac, GATEWAY, CLIENT, DHCP_CLIENT_PORT, 16);
	put_be16(frame + IP_PAYLOAD + 4, 400);
	deliver(wire, frame, length);

	build_icmp(frame, client_mac, GATEWAY, CLIENT, echo_message, sizeof(echo_message));
	deliver(wire, frame, ETHER_HEADER + 10);

	length = build_icmp(frame, gateway_mac, GATEWAY, CLIENT, echo_message, sizeof(echo_message));
	deliver(wire, frame, length);

	length = build_icmp(frame, client_mac, GATEWAY, CLIENT, echo_message, sizeof(echo_message));
	put_be16(frame + ETHER_TYPE_OFFSET, TYPE_IPV6);
	deliver(wire, frame, length);

	deliver(wire, frame, ETHER_HEADER - 4);

	net_poll(&wire->dev);
	CHECK_EQ(wire->frames_sent, 0);
	CHECK_EQ(wire->dev.stats.dropped - dropped, 7);
}

TEST(net, survives_mangled_frames)
{
	struct wire *wire = configured_wire();
	uint8_t templates[4][NET_FRAME_MAX];
	uint8_t frame[NET_FRAME_MAX];
	size_t lengths[4];
	uint8_t *original;
	unsigned int round;

	CHECK(wire != NULL);
	memset(templates, 0, sizeof(templates));
	lengths[0] = build_arp(templates[0], ARP_REQUEST, broadcast_mac, GATEWAY, zero_mac, CLIENT);
	lengths[1] = build_icmp(templates[1], client_mac, GATEWAY, CLIENT, echo_message, sizeof(echo_message));
	lengths[2] = build_udp(templates[2], client_mac, GATEWAY, CLIENT, DHCP_CLIENT_PORT, 32);
	lengths[3] = build_icmp_with_options(templates[3], echo_message, sizeof(echo_message));

	wire->guarded = test_guarded(NET_FRAME_MAX);
	CHECK(wire->guarded != NULL);
	original = wire->dev.receive_frame;

	for (round = 0; round < FUZZ_ROUNDS; round++) {
		unsigned int pick = test_random(&wire->random) % 4;

		memcpy(frame, templates[pick], NET_FRAME_MAX);
		deliver(wire, frame, mangle(wire, frame, lengths[pick], ETHER_HEADER));
		net_poll(&wire->dev);
	}

	wire->dev.receive_frame = original;
	test_release(wire->guarded, NET_FRAME_MAX);
	wire->guarded = NULL;

	CHECK_EQ(wire->malformed, 0);
	CHECK(wire->frames_sent > 0);
}

TEST(net, survives_mangled_dhcp_replies)
{
	struct wire *wire = wire_create();
	unsigned int configured = 0;
	unsigned int round;
	uint8_t *original;

	CHECK(wire != NULL);
	wire->dhcp = DHCP_MANGLE;
	wire->guarded = test_guarded(NET_FRAME_MAX);
	CHECK(wire->guarded != NULL);
	original = wire->dev.receive_frame;

	for (round = 0; round < DHCP_ROUNDS; round++) {
		if (dhcp_configure(&wire->dev) == 0) {
			configured++;
			CHECK(wire->dev.config.address != 0 && wire->dev.config.address != 0xffffffffU);
			CHECK(valid_netmask(wire->dev.config.netmask));
		}
		wire->inbox_count = 0;
	}

	wire->dev.receive_frame = original;
	test_release(wire->guarded, NET_FRAME_MAX);
	wire->guarded = NULL;

	CHECK_EQ(wire->malformed, 0);
	CHECK(configured > 0);
	CHECK(configured < DHCP_ROUNDS);
}
