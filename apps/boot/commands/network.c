#include <errno.h>
#include <lib/net.h>
#include <stdio.h>
#include <string.h>
#include <sys/console.h>
#include <sys/shell.h>
#include <sys/time.h>

#include "../private.h"

#define PING_COUNT		4
#define PING_COUNT_MAX		1000
#define PING_TIMEOUT_US		1000000
#define PING_INTERVAL_US	1000000
#define MILLISECONDS_TEXT	24

static void format_milliseconds(char *text, uint64_t microseconds)
{
	snprintf(text, MILLISECONDS_TEXT, "%lu.%03lu", microseconds / USEC_PER_MSEC, microseconds % USEC_PER_MSEC);
}

static void print_address(const char *label, uint32_t address)
{
	char text[NET_ADDRESS_TEXT];

	net_format_address(address, text);
	printf("  %-8s %s\n", label, text);
}

static void print_lease(const struct net_config *config)
{
	uint64_t held = (time_us() - config->acquired) / USEC_PER_SEC;
	char server[NET_ADDRESS_TEXT];

	net_format_address(config->server, server);
	printf("  %-8s %u s from %s, %lu s left\n", "lease", config->lease, server,
	       held < config->lease ? config->lease - held : 0);
}

static void show(struct netdev *dev)
{
	const struct net_config *config = &dev->config;
	char mac[NET_MAC_TEXT];
	char address[NET_ADDRESS_TEXT];

	net_format_mac(dev->mac, mac);
	printf("%s  %s  link %s\n", dev->name, mac, net_link_up(dev) ? "up" : "down");

	if (!net_configured(dev)) {
		printf("  %-8s none, run dhcp or net %s address/prefix [gateway]\n", "address", dev->name);
	} else {
		net_format_address(config->address, address);
		printf("  %-8s %s/%u, %s\n", "address", address, net_prefix_length(config->netmask),
		       config->dynamic ? "dhcp" : "static");
		if (config->gateway != 0) {
			print_address("gateway", config->gateway);
		}
		if (config->dns != 0) {
			print_address("dns", config->dns);
		}
		if (config->dynamic) {
			print_lease(config);
		}
	}

	printf("  %-8s %lu received, %lu sent, %lu dropped, %lu errors\n", "frames", dev->stats.received,
	       dev->stats.sent, dev->stats.dropped, dev->stats.errors);
}

static struct netdev *interface_named(const char *name)
{
	struct netdev *dev = net_find(name);

	if (dev == NULL) {
		printf("net: no interface '%s'\n", name);
	}

	return dev;
}

COMMAND(net, "[interface [address/prefix [gateway]]]", "show or configure network interfaces")
{
	uint32_t gateway = 0;
	uint32_t address;
	uint32_t netmask;
	struct netdev *dev;

	if (argc > 4) {
		return SHELL_USAGE;
	}

	if (argc == 1) {
		if (net_list() == NULL) {
			printf("no network interfaces\n");
		}

		for (dev = net_list(); dev != NULL; dev = dev->next) {
			show(dev);
		}

		return 0;
	}

	dev = interface_named(argv[1]);
	if (dev == NULL) {
		return -ECANCELED;
	}

	if (argc > 2 && (net_parse_prefix(argv[2], &address, &netmask) != 0 ||
			 (argc == 4 && net_parse_address(argv[3], &gateway) != 0))) {
		return SHELL_USAGE;
	}

	if (argc > 2) {
		net_configure(dev, address, netmask, gateway);
	}

	show(dev);

	return 0;
}

COMMAND(dhcp, "[interface]", "configure a network interface with dhcp")
{
	struct netdev *dev;
	int err;

	if (argc > 2) {
		return SHELL_USAGE;
	}

	dev = argc == 2 ? interface_named(argv[1]) : net_list();
	if (dev == NULL) {
		if (argc == 1) {
			printf("no network interfaces\n");
		}
		return -ECANCELED;
	}

	err = dhcp_configure(dev);
	if (err != 0) {
		printf("dhcp: %s: %s\n", dev->name, strerror(-err));
		return -ECANCELED;
	}

	show(dev);

	return 0;
}

struct ping_summary {
	unsigned int	sent;
	unsigned int	received;
	uint64_t	fastest;
	uint64_t	slowest;
	uint64_t	total;
};

static void print_summary(const char *target, const struct ping_summary *summary)
{
	char fastest[MILLISECONDS_TEXT];
	char average[MILLISECONDS_TEXT];
	char slowest[MILLISECONDS_TEXT];

	printf("%s: %u sent, %u received", target, summary->sent, summary->received);

	if (summary->received > 0) {
		format_milliseconds(fastest, summary->fastest);
		format_milliseconds(average, summary->total / summary->received);
		format_milliseconds(slowest, summary->slowest);
		printf(", rtt %s/%s/%s ms", fastest, average, slowest);
	}

	printf("\n");
}

static void pause_between(struct netdev *dev, uint64_t start)
{
	while (time_us() - start < PING_INTERVAL_US) {
		net_poll(dev);
	}
}

COMMAND(ping, "address [count]", "send icmp echo requests, any key stops")
{
	struct ping_summary summary = { 0, 0, UINT64_MAX, 0, 0 };
	uint64_t count = PING_COUNT;
	uint32_t address;
	struct netdev *dev;

	if (argc < 2 || argc > 3 || net_parse_address(argv[1], &address) != 0 ||
	    (argc == 3 && (parse_number(argv[2], &count) != 0 || count == 0 || count > PING_COUNT_MAX))) {
		return SHELL_USAGE;
	}

	dev = net_route(address);
	if (dev == NULL) {
		printf("ping: no configured interface reaches %s, run dhcp first\n", argv[1]);
		return -ECANCELED;
	}

	while (summary.sent < count) {
		uint64_t start = time_us();
		char milliseconds[MILLISECONDS_TEXT];
		uint64_t elapsed;
		int err = icmp_ping(dev, address, (uint16_t)(summary.sent + 1), PING_TIMEOUT_US, &elapsed);

		summary.sent++;

		if (err == 0) {
			summary.received++;
			summary.total += elapsed;
			summary.fastest = elapsed < summary.fastest ? elapsed : summary.fastest;
			summary.slowest = elapsed > summary.slowest ? elapsed : summary.slowest;
			format_milliseconds(milliseconds, elapsed);
			printf("  reply from %s: seq %u, %s ms\n", argv[1], summary.sent, milliseconds);
		} else if (err == -ETIMEDOUT) {
			printf("  no reply: seq %u\n", summary.sent);
		} else {
			printf("ping: %s: %s\n", argv[1], strerror(-err));
			break;
		}

		if (console_poll() >= 0) {
			break;
		}

		if (summary.sent < count) {
			pause_between(dev, start);
		}
	}

	print_summary(argv[1], &summary);

	return summary.received > 0 ? 0 : -ECANCELED;
}
