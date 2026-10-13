#include <drivers/pci.h>
#include <stdio.h>
#include <sys/format.h>
#include <sys/shell.h>

#include "../private.h"

#define ADDRESS_TEXT	24

static void print_bar(unsigned int index, const struct pci_bar *bar)
{
	char address[ADDRESS_TEXT] = "unassigned";
	char size[SIZE_TEXT];

	if (bar->address != 0) {
		snprintf(address, sizeof(address), "%#lx", bar->address);
	}

	format_size(size, sizeof(size), bar->size);
	printf("    bar%u  %-6s  %-12s  %s%s%s\n", index, bar->space == PCI_SPACE_IO ? "io" : "memory", address, size,
	       bar->wide ? ", 64-bit" : "", bar->prefetchable ? ", prefetchable" : "");
}

COMMAND(pci, "", "list pci devices with their resources and drivers")
{
	const struct pci_device *device;
	unsigned int i;

	if (argc != 1) {
		return SHELL_USAGE;
	}

	if (pci_devices() == NULL) {
		printf("no pci devices\n");
		return 0;
	}

	for (device = pci_devices(); device != NULL; device = device->next) {
		printf("  %02x:%02x.%u  %04x:%04x  %-14s  %s\n", device->bus, device->slot, device->function,
		       device->vendor, device->device, pci_class_name(device->class),
		       device->driver != NULL ? device->driver : "-");

		for (i = 0; i < PCI_BARS_MAX; i++) {
			if (device->bars[i].space != PCI_SPACE_NONE) {
				print_bar(i, &device->bars[i]);
			}
		}
	}

	return 0;
}
