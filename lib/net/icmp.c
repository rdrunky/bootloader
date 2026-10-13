#include <errno.h>
#include <sys/endian.h>
#include <sys/time.h>

#include "private.h"

#define ICMP_TYPE		0
#define ICMP_CODE		1
#define ICMP_CHECKSUM		2
#define ICMP_IDENTIFIER		4
#define ICMP_SEQUENCE		6
#define ICMP_HEADER_SIZE	8

#define ICMP_ECHO_REPLY		0
#define ICMP_ECHO_REQUEST	8
#define ECHO_DATA_SIZE		56

struct echo {
	uint32_t	address;
	uint16_t	identifier;
	uint16_t	sequence;
	bool		waiting;
	uint64_t	answered;
};

static struct echo echo;
static uint16_t identifier;

static void set_checksum(uint8_t *message, size_t length)
{
	put_be16(message + ICMP_CHECKSUM, 0);
	put_be16(message + ICMP_CHECKSUM, net_checksum_finish(net_checksum_add(0, message, length)));
}

static bool echo_matches(const struct ipv4_packet *packet, const uint8_t *message)
{
	return echo.waiting && packet->source == echo.address &&
	       get_be16(message + ICMP_IDENTIFIER) == echo.identifier &&
	       get_be16(message + ICMP_SEQUENCE) == echo.sequence;
}

void icmp_receive(const struct ipv4_packet *packet)
{
	struct netdev *dev = packet->dev;
	uint8_t *message = packet->payload;

	if (packet->length < ICMP_HEADER_SIZE || net_checksum_finish(net_checksum_add(0, message, packet->length)) != 0) {
		dev->stats.dropped++;
		return;
	}

	if (message[ICMP_TYPE] == ICMP_ECHO_REQUEST && message[ICMP_CODE] == 0 && net_configured(dev) &&
	    packet->destination == dev->config.address) {
		message[ICMP_TYPE] = ICMP_ECHO_REPLY;
		set_checksum(message, packet->length);
		ipv4_reply(packet, packet->length);
		return;
	}

	if (message[ICMP_TYPE] == ICMP_ECHO_REPLY && echo_matches(packet, message)) {
		echo.waiting = false;
		echo.answered = time_us();
	}
}

int icmp_ping(struct netdev *dev, uint32_t address, uint16_t sequence, uint64_t timeout, uint64_t *elapsed)
{
	uint8_t *message = dev->transmit_frame + IPV4_PAYLOAD;
	uint64_t start;
	size_t i;
	int err;

	if (identifier == 0) {
		identifier = (uint16_t)(net_random() | 1);
	}

	message[ICMP_TYPE] = ICMP_ECHO_REQUEST;
	message[ICMP_CODE] = 0;
	put_be16(message + ICMP_IDENTIFIER, identifier);
	put_be16(message + ICMP_SEQUENCE, sequence);
	for (i = 0; i < ECHO_DATA_SIZE; i++) {
		message[ICMP_HEADER_SIZE + i] = (uint8_t)i;
	}
	set_checksum(message, ICMP_HEADER_SIZE + ECHO_DATA_SIZE);

	echo.address = address;
	echo.identifier = identifier;
	echo.sequence = sequence;
	echo.waiting = true;

	err = ipv4_send(dev, address, IPV4_PROTOCOL_ICMP, dev->transmit_frame, ICMP_HEADER_SIZE + ECHO_DATA_SIZE);
	start = time_us();

	while (err == 0 && echo.waiting && time_us() - start < timeout) {
		net_poll(dev);
	}

	if (err == 0 && echo.waiting) {
		err = -ETIMEDOUT;
	}

	if (err == 0) {
		*elapsed = echo.answered - start;
	}

	echo.waiting = false;

	return err;
}
