#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>
#include <sys/time.h>

#include "private.h"

#define POLL_BUDGET		32
#define OCTETS			4
#define OCTET_DIGITS_MAX	3
#define OCTET_MAX		255
#define PREFIX_DIGITS_MAX	2
#define PREFIX_MAX		32

static struct netdev *interfaces;
static struct netdev *interfaces_last;
static unsigned int interface_count;
static uint64_t random_state;

static void stir_random(uint64_t value)
{
	random_state ^= value;
	if (random_state == 0) {
		random_state = 1;
	}
}

uint32_t net_random(void)
{
	if (random_state == 0) {
		stir_random(time_us());
	}

	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;

	return (uint32_t)(random_state >> 32);
}

int net_register(struct netdev *dev)
{
	dev->receive_frame = malloc(NET_FRAME_MAX);
	dev->transmit_frame = malloc(NET_FRAME_MAX);
	if (dev->receive_frame == NULL || dev->transmit_frame == NULL) {
		free(dev->receive_frame);
		free(dev->transmit_frame);
		return -ENOMEM;
	}

	snprintf(dev->name, sizeof(dev->name), "net%u", interface_count++);
	memset(&dev->config, 0, sizeof(dev->config));
	memset(&dev->stats, 0, sizeof(dev->stats));
	stir_random(time_us() ^ (uint64_t)get_be32(dev->mac) << 16 ^ get_be16(dev->mac + 4));

	dev->next = NULL;
	if (interfaces_last == NULL) {
		interfaces = dev;
	} else {
		interfaces_last->next = dev;
	}
	interfaces_last = dev;

	return 0;
}

struct netdev *net_list(void)
{
	return interfaces;
}

struct netdev *net_find(const char *name)
{
	struct netdev *dev;

	for (dev = interfaces; dev != NULL; dev = dev->next) {
		if (strcmp(dev->name, name) == 0) {
			return dev;
		}
	}

	return NULL;
}

bool net_configured(const struct netdev *dev)
{
	return dev->config.address != IPV4_ANY;
}

bool net_local(const struct netdev *dev, uint32_t address)
{
	return net_configured(dev) && ((address ^ dev->config.address) & dev->config.netmask) == 0;
}

struct netdev *net_route(uint32_t destination)
{
	struct netdev *dev;

	for (dev = interfaces; dev != NULL; dev = dev->next) {
		if (net_local(dev, destination)) {
			return dev;
		}
	}

	for (dev = interfaces; dev != NULL; dev = dev->next) {
		if (net_configured(dev) && dev->config.gateway != IPV4_ANY) {
			return dev;
		}
	}

	return NULL;
}

bool net_link_up(struct netdev *dev)
{
	return dev->ops->link_up == NULL || dev->ops->link_up(dev);
}

void net_configure(struct netdev *dev, uint32_t address, uint32_t netmask, uint32_t gateway)
{
	memset(&dev->config, 0, sizeof(dev->config));
	dev->config.address = address;
	dev->config.netmask = netmask;
	dev->config.gateway = gateway;
	dev->config.acquired = time_us();
	arp_forget(dev);
}

void net_poll(struct netdev *dev)
{
	unsigned int handled;

	for (handled = 0; handled < POLL_BUDGET; handled++) {
		size_t length;
		int err = dev->ops->receive(dev, dev->receive_frame, NET_FRAME_MAX, &length);

		if (err == -EAGAIN) {
			return;
		}

		if (err != 0) {
			dev->stats.errors++;
			return;
		}

		dev->stats.received++;
		ether_receive(dev, dev->receive_frame, length);
	}
}

uint32_t net_checksum_add(uint32_t sum, const void *data, size_t length)
{
	const uint8_t *bytes = data;
	size_t i;

	for (i = 0; i + 1 < length; i += 2) {
		sum += (uint32_t)(bytes[i] << 8 | bytes[i + 1]);
	}

	if (length % 2 != 0) {
		sum += (uint32_t)bytes[length - 1] << 8;
	}

	return sum;
}

uint16_t net_checksum_finish(uint32_t sum)
{
	while (sum > UINT16_MAX) {
		sum = (sum & UINT16_MAX) + (sum >> 16);
	}

	return (uint16_t)~sum;
}

static const char *parse_octets(const char *text, uint32_t *address)
{
	uint32_t value = 0;
	unsigned int octet;

	for (octet = 0; octet < OCTETS; octet++) {
		const char *start = text;
		unsigned int number = 0;

		while (isdigit((unsigned char)*text) && text - start < OCTET_DIGITS_MAX) {
			number = number * 10 + (unsigned int)(*text++ - '0');
		}

		if (text == start || number > OCTET_MAX || (text - start > 1 && *start == '0')) {
			return NULL;
		}

		value = value << 8 | number;
		if (octet + 1 < OCTETS && *text++ != '.') {
			return NULL;
		}
	}

	*address = value;

	return text;
}

int net_parse_address(const char *text, uint32_t *address)
{
	const char *end = parse_octets(text, address);

	return end != NULL && *end == '\0' ? 0 : -EINVAL;
}

int net_parse_prefix(const char *text, uint32_t *address, uint32_t *netmask)
{
	const char *end = parse_octets(text, address);
	const char *start;
	unsigned int length = 0;

	if (end == NULL || *end++ != '/') {
		return -EINVAL;
	}

	for (start = end; isdigit((unsigned char)*end) && end - start < PREFIX_DIGITS_MAX; end++) {
		length = length * 10 + (unsigned int)(*end - '0');
	}

	if (end == start || *end != '\0' || length > PREFIX_MAX) {
		return -EINVAL;
	}

	*netmask = length == 0 ? 0 : UINT32_MAX << (PREFIX_MAX - length);

	return 0;
}

unsigned int net_prefix_length(uint32_t netmask)
{
	unsigned int length = 0;

	while (length < PREFIX_MAX && (netmask & (1U << (PREFIX_MAX - 1 - length))) != 0) {
		length++;
	}

	return length;
}

void net_format_address(uint32_t address, char *text)
{
	snprintf(text, NET_ADDRESS_TEXT, "%u.%u.%u.%u", address >> 24, (address >> 16) & OCTET_MAX,
		 (address >> 8) & OCTET_MAX, address & OCTET_MAX);
}

void net_format_mac(const uint8_t *mac, char *text)
{
	snprintf(text, NET_MAC_TEXT, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
