#include <errno.h>
#include <string.h>
#include <sys/endian.h>
#include <sys/time.h>

#include "private.h"

#define DHCP_SERVER_PORT	67
#define DHCP_CLIENT_PORT	68

#define DHCP_OPERATION		0
#define DHCP_HARDWARE_TYPE	1
#define DHCP_HARDWARE_LENGTH	2
#define DHCP_XID		4
#define DHCP_FLAGS		10
#define DHCP_YOUR_ADDRESS	16
#define DHCP_CLIENT_MAC		28
#define DHCP_MAGIC_COOKIE	236
#define DHCP_OPTIONS		240
#define DHCP_MESSAGE_SIZE	300

#define DHCP_BOOT_REQUEST	1
#define DHCP_BOOT_REPLY		2
#define DHCP_HARDWARE_ETHER	1
#define DHCP_FLAG_BROADCAST	0x8000
#define DHCP_MAGIC		0x63825363U

#define OPTION_PAD		0
#define OPTION_NETMASK		1
#define OPTION_ROUTER		3
#define OPTION_DNS		6
#define OPTION_REQUESTED	50
#define OPTION_LEASE		51
#define OPTION_MESSAGE_TYPE	53
#define OPTION_SERVER		54
#define OPTION_PARAMETERS	55
#define OPTION_CLIENT_ID	61
#define OPTION_END		255

#define DHCP_DISCOVER		1
#define DHCP_OFFER		2
#define DHCP_REQUEST		3
#define DHCP_ACK		5
#define DHCP_NAK		6

#define DHCP_ATTEMPTS		3
#define DHCP_WAIT_US		NET_SECOND_US
#define DEFAULT_NETMASK		0xffffff00U

struct dhcp_reply {
	uint8_t		type;
	uint32_t	address;
	uint32_t	server;
	uint32_t	netmask;
	uint32_t	gateway;
	uint32_t	dns;
	uint32_t	lease;
};

struct dhcp_session {
	struct netdev		*dev;
	uint32_t		xid;
	uint8_t			expected;
	bool			received;
	struct dhcp_reply	offer;
	struct dhcp_reply	reply;
};

static const uint8_t parameters[] = { OPTION_NETMASK, OPTION_ROUTER, OPTION_DNS, OPTION_LEASE, OPTION_SERVER };

static size_t put_option(uint8_t *message, size_t at, uint8_t code, const void *value, uint8_t length)
{
	message[at] = code;
	message[at + 1] = length;
	memcpy(message + at + 2, value, length);

	return at + 2 + length;
}

static size_t build(const struct dhcp_session *session, uint8_t type, uint8_t *message)
{
	uint8_t client[1 + NET_MAC_SIZE];
	uint8_t address[sizeof(uint32_t)];
	size_t at = DHCP_OPTIONS;

	memset(message, 0, DHCP_MESSAGE_SIZE);
	message[DHCP_OPERATION] = DHCP_BOOT_REQUEST;
	message[DHCP_HARDWARE_TYPE] = DHCP_HARDWARE_ETHER;
	message[DHCP_HARDWARE_LENGTH] = NET_MAC_SIZE;
	put_be32(message + DHCP_XID, session->xid);
	put_be16(message + DHCP_FLAGS, DHCP_FLAG_BROADCAST);
	memcpy(message + DHCP_CLIENT_MAC, session->dev->mac, NET_MAC_SIZE);
	put_be32(message + DHCP_MAGIC_COOKIE, DHCP_MAGIC);

	client[0] = DHCP_HARDWARE_ETHER;
	memcpy(client + 1, session->dev->mac, NET_MAC_SIZE);

	at = put_option(message, at, OPTION_MESSAGE_TYPE, &type, sizeof(type));
	at = put_option(message, at, OPTION_CLIENT_ID, client, sizeof(client));
	at = put_option(message, at, OPTION_PARAMETERS, parameters, sizeof(parameters));

	if (type == DHCP_REQUEST) {
		put_be32(address, session->offer.address);
		at = put_option(message, at, OPTION_REQUESTED, address, sizeof(address));
		put_be32(address, session->offer.server);
		at = put_option(message, at, OPTION_SERVER, address, sizeof(address));
	}

	message[at] = OPTION_END;

	return DHCP_MESSAGE_SIZE;
}

static void apply_option(struct dhcp_reply *reply, uint8_t code, const uint8_t *value, uint8_t length)
{
	switch (code) {
	case OPTION_MESSAGE_TYPE:
		reply->type = length == 1 ? value[0] : 0;
		break;
	case OPTION_NETMASK:
		reply->netmask = length == sizeof(uint32_t) ? get_be32(value) : 0;
		break;
	case OPTION_ROUTER:
		reply->gateway = length >= sizeof(uint32_t) ? get_be32(value) : 0;
		break;
	case OPTION_DNS:
		reply->dns = length >= sizeof(uint32_t) ? get_be32(value) : 0;
		break;
	case OPTION_LEASE:
		reply->lease = length == sizeof(uint32_t) ? get_be32(value) : 0;
		break;
	case OPTION_SERVER:
		reply->server = length == sizeof(uint32_t) ? get_be32(value) : 0;
		break;
	default:
		break;
	}
}

static int parse_options(const uint8_t *options, size_t length, struct dhcp_reply *reply)
{
	size_t at = 0;

	while (at < length) {
		uint8_t code = options[at++];
		uint8_t size;

		if (code == OPTION_END) {
			return 0;
		}

		if (code == OPTION_PAD) {
			continue;
		}

		if (at >= length || options[at] > length - at - 1) {
			return -EBADMSG;
		}

		size = options[at++];
		apply_option(reply, code, options + at, size);
		at += size;
	}

	return -EBADMSG;
}

static bool valid_netmask(uint32_t netmask)
{
	uint32_t hosts = ~netmask;

	return netmask != 0 && (hosts & (hosts + 1)) == 0;
}

static bool usable(const struct dhcp_reply *reply)
{
	return reply->address != IPV4_ANY && reply->address != IPV4_BROADCAST && valid_netmask(reply->netmask) &&
	       reply->server != IPV4_ANY;
}

static void receive_reply(void *context, const struct ipv4_packet *packet, uint16_t source_port,
			  const uint8_t *message, size_t length)
{
	struct dhcp_session *session = context;
	struct dhcp_reply reply;

	if (packet->dev != session->dev || source_port != DHCP_SERVER_PORT || length < DHCP_OPTIONS ||
	    message[DHCP_OPERATION] != DHCP_BOOT_REPLY || get_be32(message + DHCP_XID) != session->xid ||
	    memcmp(message + DHCP_CLIENT_MAC, session->dev->mac, NET_MAC_SIZE) != 0 ||
	    get_be32(message + DHCP_MAGIC_COOKIE) != DHCP_MAGIC) {
		return;
	}

	memset(&reply, 0, sizeof(reply));
	reply.address = get_be32(message + DHCP_YOUR_ADDRESS);
	reply.netmask = DEFAULT_NETMASK;
	reply.server = packet->source;
	if (parse_options(message + DHCP_OPTIONS, length - DHCP_OPTIONS, &reply) != 0) {
		return;
	}

	if (reply.type == DHCP_NAK || (reply.type == session->expected && usable(&reply))) {
		session->reply = reply;
		session->received = true;
	}
}

static int exchange(struct dhcp_session *session, uint8_t type, uint8_t expected)
{
	uint8_t message[DHCP_MESSAGE_SIZE];
	uint64_t wait = DHCP_WAIT_US;
	unsigned int attempt;

	session->expected = expected;

	for (attempt = 0; attempt < DHCP_ATTEMPTS; attempt++, wait *= 2) {
		size_t length = build(session, type, message);
		uint64_t start = time_us();
		int err;

		session->received = false;
		err = udp_send(session->dev, IPV4_BROADCAST, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, message, length);
		if (err != 0) {
			return err;
		}

		while (!session->received && time_us() - start < wait) {
			net_poll(session->dev);
		}

		if (session->received) {
			return session->reply.type == DHCP_NAK ? -ECONNREFUSED : 0;
		}
	}

	return -ETIMEDOUT;
}

int dhcp_configure(struct netdev *dev)
{
	struct dhcp_session session;
	int err;

	memset(&session, 0, sizeof(session));
	session.dev = dev;
	session.xid = net_random();

	err = udp_bind(DHCP_CLIENT_PORT, receive_reply, &session);
	if (err != 0) {
		return err;
	}

	net_configure(dev, IPV4_ANY, 0, IPV4_ANY);

	err = exchange(&session, DHCP_DISCOVER, DHCP_OFFER);
	if (err == 0) {
		session.offer = session.reply;
		err = exchange(&session, DHCP_REQUEST, DHCP_ACK);
	}

	udp_unbind(DHCP_CLIENT_PORT);

	if (err == 0) {
		net_configure(dev, session.reply.address, session.reply.netmask, session.reply.gateway);
		dev->config.dns = session.reply.dns;
		dev->config.server = session.reply.server;
		dev->config.lease = session.reply.lease;
		dev->config.dynamic = true;
	}

	return err;
}
