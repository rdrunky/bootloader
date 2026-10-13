#include <lib/cksum.h>
#include <lib/devicetree.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/endian.h>

#define UART_BASE	0x09000000UL
#define UART_DATA	0
#define UART_FLAGS	6
#define UART_TX_FULL	(1u << 5)
#define LINE_MAX	256
#define SCTLR_M		1u

#define VIRTIO_MMIO_MAGIC	0x74726976
#define VIRTIO_MMIO_DEVICE_ID	0x008
#define VIRTIO_MMIO_STATUS	0x070

#define PCI_SLOTS		32
#define PCI_SLOT_SHIFT		15
#define PCI_COMMAND		0x04
#define PCI_VENDOR_NONE		0xffff

void payload_main(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t el);

static void put_char(char c)
{
	volatile uint32_t *uart = (volatile uint32_t *)UART_BASE;

	while ((uart[UART_FLAGS] & UART_TX_FULL) != 0) {
	}

	uart[UART_DATA] = (uint8_t)c;
}

static void __attribute__((format(printf, 1, 2))) say(const char *fmt, ...)
{
	char line[LINE_MAX];
	const char *c;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);

	for (c = line; *c != '\0'; c++) {
		put_char(*c);
	}

	put_char('\r');
	put_char('\n');
}

static uint64_t system_control(uint64_t el)
{
	uint64_t value;

	if (el == 2) {
		__asm__ volatile("mrs %0, sctlr_el2" : "=r"(value));
	} else {
		__asm__ volatile("mrs %0, sctlr_el1" : "=r"(value));
	}

	return value;
}

static const char *text(const struct dt *dt, int node, const char *name)
{
	const char *value = dt_read_string(dt, node, name);

	return value != NULL ? value : "-";
}

static void report_ramdisk(const struct dt *dt)
{
	uint32_t length;
	const uint8_t *range = dt_property(dt, dt_find(dt, "/chosen/memory-map"), "ramdisk", &length);
	uint64_t base;
	uint64_t size;

	if (range == NULL || length != 2 * sizeof(uint64_t)) {
		say("payload: no ramdisk");
		return;
	}

	base = get_be64(range);
	size = get_be64(range + sizeof(uint64_t));
	say("payload: ramdisk %#lx %lu crc32 %#x", base, size, crc32((const void *)(uintptr_t)base, (size_t)size));
}

static void report_seed(const struct dt *dt, int chosen)
{
	uint32_t length;
	const uint8_t *seed = dt_property(dt, chosen, "boot-seed", &length);

	if (seed == NULL) {
		say("payload: no boot-seed");
		return;
	}

	say("payload: boot-seed %u bytes crc32 %#x", length, crc32(seed, length));
}

static uint64_t line_length(const char *text, uint64_t size)
{
	uint64_t length = 0;

	while (length < size && text[length] != '\n') {
		length++;
	}

	return length;
}

static uint64_t last_line(const char *text, uint64_t size)
{
	uint64_t start = size;

	if (start > 0 && text[start - 1] == '\n') {
		start--;
	}

	while (start > 0 && text[start - 1] != '\n') {
		start--;
	}

	return start;
}

static void report_log(const struct dt *dt)
{
	uint32_t length;
	const uint8_t *range = dt_property(dt, dt_find(dt, "/chosen/memory-map"), "boot-log", &length);
	const char *log;
	uint64_t size;
	uint64_t start;

	if (range == NULL || length != 2 * sizeof(uint64_t)) {
		say("payload: no boot-log");
		return;
	}

	log = (const char *)(uintptr_t)get_be64(range);
	size = get_be64(range + sizeof(uint64_t));
	start = last_line(log, size);

	say("payload: boot-log %lu bytes", size);
	say("payload: boot-log first '%.*s'", (int)line_length(log, size), log);
	say("payload: boot-log last '%.*s'", (int)line_length(log + start, size - start), log + start);
}

static uint32_t read32(uint64_t address)
{
	return *(volatile uint32_t *)(uintptr_t)address;
}

static void report_virtio(const struct dt *dt)
{
	int node;

	for (node = dt_find_compatible(dt, -1, "virtio,mmio"); node >= 0;
	     node = dt_find_compatible(dt, node, "virtio,mmio")) {
		uint64_t base;
		uint64_t size;

		if (dt_reg(dt, node, 0, &base, &size) != 0 || read32(base) != VIRTIO_MMIO_MAGIC ||
		    read32(base + VIRTIO_MMIO_DEVICE_ID) == 0) {
			continue;
		}

		say("payload: virtio-mmio %#lx device %u status %u", base, read32(base + VIRTIO_MMIO_DEVICE_ID),
		    read32(base + VIRTIO_MMIO_STATUS));
	}
}

static void report_partitions(const struct dt *dt)
{
	int property;

	say("payload: boot-slot '%s'", text(dt, dt_find(dt, "/chosen"), "boot-slot"));

	for (property = dt_first_property(dt, dt_find(dt, "/chosen/partitions")); property >= 0;
	     property = dt_next_property(dt, property)) {
		const char *name;
		const char *value = dt_property_at(dt, property, &name, NULL);

		say("payload: partition %s %s", name, value);
	}
}

static void report_pci(const struct dt *dt)
{
	int node = dt_find_compatible(dt, -1, "pci-host-ecam-generic");
	uint64_t base;
	uint64_t size;
	unsigned int slot;

	if (node < 0 || dt_reg(dt, node, 0, &base, &size) != 0) {
		return;
	}

	for (slot = 0; slot < PCI_SLOTS; slot++) {
		uint64_t config = base + ((uint64_t)slot << PCI_SLOT_SHIFT);
		uint32_t id = read32(config);

		if ((id & PCI_VENDOR_NONE) == PCI_VENDOR_NONE) {
			continue;
		}

		say("payload: pci 00:%02x.0 %04x:%04x command %#x", slot, id & PCI_VENDOR_NONE, id >> 16,
		    read32(config + PCI_COMMAND) & 0xffff);
	}
}

void payload_main(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t el)
{
	uint32_t verified = 0;
	struct dt dt;
	int chosen;

	say("payload: el%lu x0=%#lx x1=%#lx x2=%#lx x3=%#lx mmu %s", el, x0, x1, x2, x3,
	    (system_control(el) & SCTLR_M) != 0 ? "on" : "off");

	if (dt_open(&dt, (const void *)(uintptr_t)x0, DT_SIZE_MAX) != 0) {
		say("payload: no device tree");
		return;
	}

	chosen = dt_find(&dt, "/chosen");
	dt_read_u32(&dt, chosen, "boot-verified", &verified);

	say("payload: boot-mode '%s'", text(&dt, chosen, "boot-mode"));
	say("payload: boot-verified %u", verified);
	say("payload: boot-policy '%s'", text(&dt, chosen, "boot-policy"));
	say("payload: boot-args '%s'", text(&dt, chosen, "boot-args"));
	report_seed(&dt, chosen);
	report_log(&dt);
	report_ramdisk(&dt);
	report_partitions(&dt);
	report_virtio(&dt);
	report_pci(&dt);
	say("payload: done");
}
