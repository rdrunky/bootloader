#ifndef PCI_PRIVATE_H
#define PCI_PRIVATE_H

#include <drivers/pci.h>
#include <stdint.h>

#define PCI_BUS_SHIFT		20
#define PCI_SLOT_SHIFT		15
#define PCI_FUNCTION_SHIFT	12
#define PCI_BUS_LAST		255

struct pci_window {
	uint64_t	cpu;
	uint64_t	bus;
	uint64_t	size;
	uint64_t	next;
};

struct pci_host {
	uintptr_t		config;
	unsigned int		bus_first;
	unsigned int		bus_last;
	unsigned int		bus_next;
	bool			coherent;
	struct pci_window	io;
	struct pci_window	memory;
	struct pci_window	memory64;
};

int pci_host_scan(struct pci_host *host);

#endif
