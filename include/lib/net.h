#ifndef LIB_NET_H
#define LIB_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NET_NAME_MAX		16
#define NET_MAC_SIZE		6
#define NET_FRAME_MAX		1514
#define NET_ADDRESS_TEXT	16
#define NET_MAC_TEXT		18

struct netdev;

struct netdev_ops {
	int	(*send)(struct netdev *dev, const void *frame, size_t length);
	int	(*receive)(struct netdev *dev, void *frame, size_t size, size_t *length);
	bool	(*link_up)(struct netdev *dev);
};

struct net_config {
	uint32_t	address;
	uint32_t	netmask;
	uint32_t	gateway;
	uint32_t	dns;
	uint32_t	server;
	uint32_t	lease;
	uint64_t	acquired;
	bool		dynamic;
};

struct net_stats {
	uint64_t	received;
	uint64_t	sent;
	uint64_t	dropped;
	uint64_t	errors;
};

struct netdev {
	struct netdev		*next;
	char			name[NET_NAME_MAX];
	uint8_t			mac[NET_MAC_SIZE];
	const struct netdev_ops	*ops;
	void			*context;
	struct net_config	config;
	struct net_stats	stats;
	uint8_t			*receive_frame;
	uint8_t			*transmit_frame;
};

int net_register(struct netdev *dev);
struct netdev *net_list(void);
struct netdev *net_find(const char *name);
struct netdev *net_route(uint32_t destination);

bool net_link_up(struct netdev *dev);
bool net_configured(const struct netdev *dev);
void net_configure(struct netdev *dev, uint32_t address, uint32_t netmask, uint32_t gateway);
void net_poll(struct netdev *dev);

int dhcp_configure(struct netdev *dev);
int icmp_ping(struct netdev *dev, uint32_t address, uint16_t sequence, uint64_t timeout, uint64_t *elapsed);

int net_parse_address(const char *text, uint32_t *address);
int net_parse_prefix(const char *text, uint32_t *address, uint32_t *netmask);
unsigned int net_prefix_length(uint32_t netmask);
void net_format_address(uint32_t address, char *text);
void net_format_mac(const uint8_t *mac, char *text);

#endif
