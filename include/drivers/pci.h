#ifndef DRIVERS_PCI_H
#define DRIVERS_PCI_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/linkerset.h>

#define PCI_BARS_MAX			6

#define PCI_VENDOR_ID			0x00
#define PCI_DEVICE_ID			0x02
#define PCI_COMMAND			0x04
#define PCI_STATUS			0x06
#define PCI_CLASS_REVISION		0x08
#define PCI_HEADER_TYPE			0x0e
#define PCI_BAR0			0x10
#define PCI_SUBSYSTEM_ID		0x2e
#define PCI_CAPABILITIES		0x34

#define PCI_COMMAND_IO			0x0001
#define PCI_COMMAND_MEMORY		0x0002
#define PCI_COMMAND_MASTER		0x0004
#define PCI_STATUS_CAPABILITIES		0x0010

#define PCI_CAPABILITY_VENDOR		0x09

enum pci_space {
	PCI_SPACE_NONE,
	PCI_SPACE_IO,
	PCI_SPACE_MEMORY,
};

struct pci_bar {
	enum pci_space	space;
	bool		wide;
	bool		prefetchable;
	uint64_t	size;
	uint64_t	address;
};

struct pci_host;

struct pci_device {
	struct pci_device	*next;
	struct pci_host		*host;
	uint8_t			bus;
	uint8_t			slot;
	uint8_t			function;
	uint16_t		vendor;
	uint16_t		device;
	uint32_t		class;
	uint8_t			revision;
	bool			bridge;
	bool			coherent;
	struct pci_bar		bars[PCI_BARS_MAX];
	const char		*driver;
};

struct pci_driver {
	const char	*name;
	uint16_t	vendor;
	uint16_t	device_first;
	uint16_t	device_last;
	int		(*probe)(struct pci_device *device);
};

#define PCI_DRIVER(driver)	SET_ENTRY(pci_drivers, driver)

uint8_t pci_read8(const struct pci_device *device, unsigned int offset);
uint16_t pci_read16(const struct pci_device *device, unsigned int offset);
uint32_t pci_read32(const struct pci_device *device, unsigned int offset);

int pci_next_capability(const struct pci_device *device, uint8_t id, int after);
int pci_enable(struct pci_device *device);

const struct pci_device *pci_devices(void);
const char *pci_class_name(uint32_t class);

#endif
