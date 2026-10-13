#include <drivers/pci.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/driver.h>
#include <sys/macros.h>
#include <sys/mem.h>

#include "private.h"

#define PCI_SLOTS			32
#define PCI_FUNCTIONS			8
#define PCI_DEPTH_MAX			16
#define PCI_VENDOR_NONE			0xffff
#define PCI_HEADER_MULTIFUNCTION	0x80
#define PCI_HEADER_LAYOUT		0x7f
#define PCI_HEADER_ENDPOINT		0
#define PCI_HEADER_BRIDGE		1
#define PCI_BRIDGE_BARS			2

#define PCI_BAR_IO			0x1
#define PCI_BAR_TYPE			0x6
#define PCI_BAR_TYPE_WIDE		0x4
#define PCI_BAR_PREFETCHABLE		0x8
#define PCI_BAR_IO_MASK			0xfffffffcU
#define PCI_BAR_MEMORY_MASK		0xfffffff0U
#define PCI_BAR_PROBE			0xffffffffU
#define PCI_IO_SHORT_MASK		0xffff0000U
#define PCI_MEMORY_SHORT_MASK		0xffffffff00000000ULL

#define PCI_BRIDGE_BUSES		0x18
#define PCI_BRIDGE_IO_WINDOW		0x1c
#define PCI_BRIDGE_MEMORY_WINDOW	0x20
#define PCI_BRIDGE_PREFETCH_WINDOW	0x24
#define PCI_BRIDGE_PREFETCH_BASE_UPPER	0x28
#define PCI_BRIDGE_PREFETCH_LIMIT_UPPER	0x2c
#define PCI_BRIDGE_IO_UPPER		0x30
#define PCI_BRIDGE_LATENCY		0xff000000U
#define PCI_IO_WINDOW_DISABLED		0x00f0
#define PCI_MEMORY_WINDOW_DISABLED	0x0000fff0U
#define PCI_IO_WINDOW_FIELD		0xf0
#define PCI_MEMORY_WINDOW_FIELD		0xfff0

#define BRIDGE_MEMORY_ALIGN		(1ULL << 20)
#define BRIDGE_IO_ALIGN			(1ULL << 12)
#define MEMORY_BAR_ALIGN		4096ULL
#define IO_RESERVED			0x1000ULL
#define CAPABILITY_FIRST		0x40
#define CAPABILITY_POINTER		0xfc
#define CAPABILITIES_MAX		48

SET_DECLARE(pci_drivers, const struct pci_driver);

static const char *const class_names[] = {
	"unclassified", "storage", "network", "display", "multimedia", "memory", "bridge", "communication",
	"system", "input", "docking", "processor", "serial bus", "wireless", "intelligent io", "satellite",
	"encryption", "signal processing", "accelerator", "instrumentation",
};

static struct pci_device *devices;
static struct pci_device *devices_last;

static int scan_bus(struct pci_host *host, unsigned int bus, unsigned int depth);

static uintptr_t config_address(const struct pci_device *device, unsigned int offset)
{
	const struct pci_host *host = device->host;

	return host->config + ((uintptr_t)(device->bus - host->bus_first) << PCI_BUS_SHIFT) +
	       ((uintptr_t)device->slot << PCI_SLOT_SHIFT) + ((uintptr_t)device->function << PCI_FUNCTION_SHIFT) + offset;
}

uint8_t pci_read8(const struct pci_device *device, unsigned int offset)
{
	return *(volatile uint8_t *)config_address(device, offset);
}

uint16_t pci_read16(const struct pci_device *device, unsigned int offset)
{
	return *(volatile uint16_t *)config_address(device, offset);
}

uint32_t pci_read32(const struct pci_device *device, unsigned int offset)
{
	return *(volatile uint32_t *)config_address(device, offset);
}

static void pci_write16(const struct pci_device *device, unsigned int offset, uint16_t value)
{
	*(volatile uint16_t *)config_address(device, offset) = value;
}

static void pci_write32(const struct pci_device *device, unsigned int offset, uint32_t value)
{
	*(volatile uint32_t *)config_address(device, offset) = value;
}

static bool window_take(struct pci_window *window, uint64_t size, uint64_t alignment, uint64_t *address)
{
	uint64_t start = ALIGN_UP(window->next, alignment);
	uint64_t end = window->bus + window->size;

	if (window->size == 0 || start < window->next || start > end || size > end - start) {
		return false;
	}

	window->next = start + size;
	*address = start;

	return true;
}

static uint32_t probe_register(const struct pci_device *device, unsigned int offset)
{
	uint32_t original = pci_read32(device, offset);
	uint32_t decoded;

	pci_write32(device, offset, PCI_BAR_PROBE);
	decoded = pci_read32(device, offset);
	pci_write32(device, offset, original);

	return decoded;
}

static unsigned int size_bar(struct pci_device *device, unsigned int index, unsigned int count)
{
	struct pci_bar *bar = &device->bars[index];
	unsigned int offset = PCI_BAR0 + index * 4;
	uint32_t decoded = probe_register(device, offset);
	uint64_t mask;

	if ((decoded & PCI_BAR_IO) != 0) {
		mask = decoded & PCI_BAR_IO_MASK;
		if (mask != 0 && mask <= UINT16_MAX) {
			mask |= PCI_IO_SHORT_MASK;
		}

		if (mask != 0) {
			bar->space = PCI_SPACE_IO;
			bar->size = (uint32_t)(~mask + 1);
		}

		return 1;
	}

	mask = decoded & PCI_BAR_MEMORY_MASK;
	if ((decoded & PCI_BAR_TYPE) == PCI_BAR_TYPE_WIDE && index + 1 < count) {
		mask |= (uint64_t)probe_register(device, offset + 4) << 32;
		bar->wide = true;
	} else if (mask != 0) {
		mask |= PCI_MEMORY_SHORT_MASK;
	}

	if (mask != 0) {
		bar->space = PCI_SPACE_MEMORY;
		bar->prefetchable = (decoded & PCI_BAR_PREFETCHABLE) != 0;
		bar->size = ~mask + 1;
	}

	return bar->wide ? 2 : 1;
}

static bool allocate(struct pci_host *host, struct pci_bar *bar, unsigned int depth, uint64_t *address)
{
	struct pci_window *window = bar->space == PCI_SPACE_IO ? &host->io : &host->memory;
	uint64_t alignment = bar->size;

	if (bar->space == PCI_SPACE_MEMORY && alignment < MEMORY_BAR_ALIGN) {
		alignment = MEMORY_BAR_ALIGN;
	}

	if (!window_take(window, bar->size, alignment, address)) {
		if (bar->space != PCI_SPACE_MEMORY || !bar->wide || depth > 0) {
			return false;
		}

		window = &host->memory64;
		if (!window_take(window, bar->size, alignment, address)) {
			return false;
		}
	}

	bar->address = window->cpu + (*address - window->bus);

	return true;
}

static uint16_t assign_bars(struct pci_device *device, unsigned int count, unsigned int depth)
{
	uint16_t decode = 0;
	unsigned int index = 0;

	while (index < count) {
		struct pci_bar *bar = &device->bars[index];
		unsigned int offset = PCI_BAR0 + index * 4;
		unsigned int used = size_bar(device, index, count);
		uint64_t address;

		if (bar->space != PCI_SPACE_NONE && allocate(device->host, bar, depth, &address)) {
			pci_write32(device, offset, (uint32_t)address);
			if (bar->wide) {
				pci_write32(device, offset + 4, (uint32_t)(address >> 32));
			}
			decode |= bar->space == PCI_SPACE_IO ? PCI_COMMAND_IO : PCI_COMMAND_MEMORY;
		} else if (bar->space != PCI_SPACE_NONE) {
			printf("pci %02x:%02x.%u bar%u %#lx bytes, no room\n", device->bus, device->slot,
			       device->function, index, bar->size);
		}

		index += used;
	}

	return decode;
}

static void set_memory_window(const struct pci_device *bridge, uint64_t start, uint64_t end)
{
	uint32_t value = PCI_MEMORY_WINDOW_DISABLED;

	if (end > start) {
		value = (uint32_t)((start >> 16) & PCI_MEMORY_WINDOW_FIELD) |
			(uint32_t)(((end - 1) >> 16) & PCI_MEMORY_WINDOW_FIELD) << 16;
	}

	pci_write32(bridge, PCI_BRIDGE_MEMORY_WINDOW, value);
	pci_write32(bridge, PCI_BRIDGE_PREFETCH_WINDOW, PCI_MEMORY_WINDOW_DISABLED);
	pci_write32(bridge, PCI_BRIDGE_PREFETCH_BASE_UPPER, 0);
	pci_write32(bridge, PCI_BRIDGE_PREFETCH_LIMIT_UPPER, 0);
}

static void set_io_window(const struct pci_device *bridge, uint64_t start, uint64_t end)
{
	uint16_t value = PCI_IO_WINDOW_DISABLED;
	uint32_t upper = 0;

	if (end > start) {
		value = (uint16_t)(((start >> 8) & PCI_IO_WINDOW_FIELD) | (((end - 1) >> 8) & PCI_IO_WINDOW_FIELD) << 8);
		upper = (uint32_t)(start >> 16) | (uint32_t)((end - 1) >> 16) << 16;
	}

	pci_write16(bridge, PCI_BRIDGE_IO_WINDOW, value);
	pci_write32(bridge, PCI_BRIDGE_IO_UPPER, upper);
}

static int scan_bridge(struct pci_device *bridge, unsigned int depth)
{
	struct pci_host *host = bridge->host;
	uint32_t latency = pci_read32(bridge, PCI_BRIDGE_BUSES) & PCI_BRIDGE_LATENCY;
	unsigned int secondary = host->bus_next;
	uint64_t memory_start;
	uint64_t io_start;
	int err;

	if (secondary > host->bus_last || depth >= PCI_DEPTH_MAX) {
		printf("pci %02x:%02x.%u bridge out of bus numbers\n", bridge->bus, bridge->slot, bridge->function);
		return 0;
	}

	host->bus_next++;
	pci_write32(bridge, PCI_BRIDGE_BUSES, latency | host->bus_last << 16 | secondary << 8 | bridge->bus);

	host->memory.next = ALIGN_UP(host->memory.next, BRIDGE_MEMORY_ALIGN);
	host->io.next = ALIGN_UP(host->io.next, BRIDGE_IO_ALIGN);
	memory_start = host->memory.next;
	io_start = host->io.next;

	err = scan_bus(host, secondary, depth + 1);

	pci_write32(bridge, PCI_BRIDGE_BUSES, latency | (host->bus_next - 1) << 16 | secondary << 8 | bridge->bus);

	host->memory.next = ALIGN_UP(host->memory.next, BRIDGE_MEMORY_ALIGN);
	host->io.next = ALIGN_UP(host->io.next, BRIDGE_IO_ALIGN);
	set_memory_window(bridge, memory_start, host->memory.next);
	set_io_window(bridge, io_start, host->io.next);

	pci_write16(bridge, PCI_COMMAND,
		    pci_read16(bridge, PCI_COMMAND) | PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

	return err;
}

static int add_device(const struct pci_device *probe, unsigned int layout, unsigned int depth)
{
	struct pci_device *device;
	uint32_t class_revision;
	uint16_t command;

	if (layout != PCI_HEADER_ENDPOINT && layout != PCI_HEADER_BRIDGE) {
		return 0;
	}

	device = malloc(sizeof(*device));
	if (device == NULL) {
		return -ENOMEM;
	}

	*device = *probe;
	device->vendor = pci_read16(device, PCI_VENDOR_ID);
	device->device = pci_read16(device, PCI_DEVICE_ID);
	class_revision = pci_read32(device, PCI_CLASS_REVISION);
	device->class = class_revision >> 8;
	device->revision = (uint8_t)class_revision;
	device->bridge = layout == PCI_HEADER_BRIDGE;
	device->coherent = device->host->coherent;

	if (devices_last == NULL) {
		devices = device;
	} else {
		devices_last->next = device;
	}
	devices_last = device;

	command = pci_read16(device, PCI_COMMAND) & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	pci_write16(device, PCI_COMMAND, command);
	command |= assign_bars(device, device->bridge ? PCI_BRIDGE_BARS : PCI_BARS_MAX, depth);
	pci_write16(device, PCI_COMMAND, command);

	return device->bridge ? scan_bridge(device, depth) : 0;
}

static int scan_bus(struct pci_host *host, unsigned int bus, unsigned int depth)
{
	unsigned int slot;

	for (slot = 0; slot < PCI_SLOTS; slot++) {
		unsigned int functions = 1;
		unsigned int function;

		for (function = 0; function < functions; function++) {
			struct pci_device probe = {
				.host = host,
				.bus = (uint8_t)bus,
				.slot = (uint8_t)slot,
				.function = (uint8_t)function,
			};
			uint8_t header;
			int err;

			if (pci_read16(&probe, PCI_VENDOR_ID) == PCI_VENDOR_NONE) {
				continue;
			}

			header = pci_read8(&probe, PCI_HEADER_TYPE);
			if (function == 0 && (header & PCI_HEADER_MULTIFUNCTION) != 0) {
				functions = PCI_FUNCTIONS;
			}

			err = add_device(&probe, header & PCI_HEADER_LAYOUT, depth);
			if (err != 0) {
				return err;
			}
		}
	}

	return 0;
}

static const struct pci_driver *find_driver(const struct pci_device *device)
{
	const struct pci_driver *driver;

	SET_FOREACH(driver, pci_drivers) {
		if (driver->vendor == device->vendor && device->device >= driver->device_first &&
		    device->device <= driver->device_last) {
			return driver;
		}
	}

	return NULL;
}

static void bind_drivers(const struct pci_host *host)
{
	struct pci_device *device;

	for (device = devices; device != NULL; device = device->next) {
		const struct pci_driver *driver;
		int err;

		if (device->host != host) {
			continue;
		}

		driver = find_driver(device);
		if (driver == NULL) {
			continue;
		}

		err = driver->probe(device);
		if (err == 0) {
			device->driver = driver->name;
		} else if (err != -ENODEV) {
			printf("pci %02x:%02x.%u %s %s\n", device->bus, device->slot, device->function, driver->name,
			       strerror(-err));
		}
	}
}

static void quiesce(void *context)
{
	const struct pci_device *device;

	for (device = devices; device != NULL; device = device->next) {
		if (device->host == context) {
			pci_write16(device, PCI_COMMAND, pci_read16(device, PCI_COMMAND) & ~PCI_COMMAND_MASTER);
		}
	}
}

static unsigned int device_count(const struct pci_host *host)
{
	const struct pci_device *device;
	unsigned int count = 0;

	for (device = devices; device != NULL; device = device->next) {
		if (device->host == host) {
			count++;
		}
	}

	return count;
}

int pci_host_scan(struct pci_host *host)
{
	int err = driver_quiesce_register(quiesce, host);

	if (err != 0) {
		return err;
	}

	host->bus_next = host->bus_first + 1;
	host->memory.next = host->memory.bus;
	host->memory64.next = host->memory64.bus;
	host->io.next = host->io.bus < IO_RESERVED ? IO_RESERVED : host->io.bus;

	err = scan_bus(host, host->bus_first, 0);
	printf("pci %u devices %s\n", device_count(host), err == 0 ? "ok" : strerror(-err));
	bind_drivers(host);

	return err;
}

int pci_next_capability(const struct pci_device *device, uint8_t id, int after)
{
	unsigned int offset;
	unsigned int steps;

	if ((pci_read16(device, PCI_STATUS) & PCI_STATUS_CAPABILITIES) == 0) {
		return -ENOENT;
	}

	offset = pci_read8(device, after < 0 ? PCI_CAPABILITIES : (unsigned int)after + 1);

	for (steps = 0; steps < CAPABILITIES_MAX; steps++) {
		offset &= CAPABILITY_POINTER;
		if (offset < CAPABILITY_FIRST) {
			return -ENOENT;
		}

		if (pci_read8(device, offset) == id) {
			return (int)offset;
		}

		offset = pci_read8(device, offset + 1);
	}

	return -ENOENT;
}

int pci_enable(struct pci_device *device)
{
	unsigned int i;

	for (i = 0; i < PCI_BARS_MAX; i++) {
		const struct pci_bar *bar = &device->bars[i];

		if (bar->space == PCI_SPACE_MEMORY && bar->address != 0) {
			int err = mem_map_device(bar->address, bar->size);

			if (err != 0) {
				return err;
			}
		}
	}

	pci_write16(device, PCI_COMMAND, pci_read16(device, PCI_COMMAND) | PCI_COMMAND_MASTER);

	return 0;
}

const struct pci_device *pci_devices(void)
{
	return devices;
}

const char *pci_class_name(uint32_t class)
{
	uint32_t base = class >> 16;

	return base < ARRAY_SIZE(class_names) ? class_names[base] : "other";
}
