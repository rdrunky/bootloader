#include <errno.h>
#include <lib/devicetree.h>
#include <stdlib.h>
#include <sys/driver.h>
#include <sys/endian.h>
#include <sys/mem.h>

#include "private.h"

#define PCI_ADDRESS_CELLS	3
#define SPACE_SHIFT		24
#define SPACE_MASK		0x3
#define SPACE_IO		1
#define SPACE_MEMORY32		2
#define SPACE_MEMORY64		3

static struct pci_window *window_for(struct pci_host *host, uint32_t space)
{
	switch ((space >> SPACE_SHIFT) & SPACE_MASK) {
	case SPACE_IO:
		return &host->io;
	case SPACE_MEMORY32:
		return &host->memory;
	case SPACE_MEMORY64:
		return &host->memory64;
	default:
		return NULL;
	}
}

static int parse_ranges(const struct dt *dt, int node, struct pci_host *host)
{
	uint32_t parent_cells = dt_address_cells(dt, dt_parent(dt, node));
	uint32_t size_cells = dt_size_cells(dt, node);
	uint32_t stride = (PCI_ADDRESS_CELLS + parent_cells + size_cells) * DT_CELL_SIZE;
	uint32_t length;
	const uint8_t *ranges = dt_property(dt, node, "ranges", &length);
	uint32_t at;

	if (ranges == NULL || parent_cells == 0 || parent_cells > DT_CELLS_MAX || size_cells == 0 ||
	    size_cells > DT_CELLS_MAX || length % stride != 0) {
		return -EINVAL;
	}

	for (at = 0; at < length; at += stride) {
		const uint8_t *entry = ranges + at;
		struct pci_window *window = window_for(host, get_be32(entry));
		uint64_t cpu = dt_read_cells(entry + PCI_ADDRESS_CELLS * DT_CELL_SIZE, parent_cells);
		int err;

		if (window == NULL || window->size != 0) {
			continue;
		}

		err = dt_translate(dt, node, &cpu);
		if (err != 0) {
			return err;
		}

		window->cpu = cpu;
		window->bus = dt_read_cells(entry + DT_CELL_SIZE, PCI_ADDRESS_CELLS - 1);
		window->size = dt_read_cells(entry + (PCI_ADDRESS_CELLS + parent_cells) * DT_CELL_SIZE, size_cells);
	}

	return host->memory.size != 0 || host->memory64.size != 0 ? 0 : -EINVAL;
}

static int parse_buses(const struct dt *dt, int node, uint64_t config_size, struct pci_host *host)
{
	uint64_t buses = config_size >> PCI_BUS_SHIFT;
	uint32_t length;
	const uint8_t *range = dt_property(dt, node, "bus-range", &length);

	host->bus_first = 0;
	host->bus_last = PCI_BUS_LAST;

	if (range != NULL) {
		if (length != 2 * DT_CELL_SIZE) {
			return -EINVAL;
		}

		host->bus_first = get_be32(range);
		host->bus_last = get_be32(range + DT_CELL_SIZE);
	}

	if (buses == 0 || host->bus_first > host->bus_last || host->bus_last > PCI_BUS_LAST) {
		return -EINVAL;
	}

	if (host->bus_last - host->bus_first + 1 > buses) {
		host->bus_last = host->bus_first + (unsigned int)buses - 1;
	}

	return 0;
}

static int ecam_probe(const struct dt *dt, int node)
{
	struct pci_host *host = calloc(1, sizeof(*host));
	uint64_t base;
	uint64_t size;
	int err;

	if (host == NULL) {
		return -ENOMEM;
	}

	err = dt_reg(dt, node, 0, &base, &size);
	if (err == 0) {
		err = parse_buses(dt, node, size, host);
	}

	if (err == 0) {
		err = parse_ranges(dt, node, host);
	}

	if (err == 0) {
		err = mem_map_device(base, (uint64_t)(host->bus_last - host->bus_first + 1) << PCI_BUS_SHIFT);
	}

	if (err != 0) {
		free(host);
		return err;
	}

	host->config = (uintptr_t)base;
	host->coherent = dt_property(dt, node, "dma-coherent", NULL) != NULL;

	return pci_host_scan(host);
}

static const char *const ecam_compatible[] = { "pci-host-ecam-generic", NULL };

static const struct driver ecam_driver = {
	.name		= "pci-ecam",
	.compatible	= ecam_compatible,
	.probe		= ecam_probe,
};

DRIVER(ecam_driver);
