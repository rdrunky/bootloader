#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/console.h>
#include <sys/driver.h>
#include <sys/macros.h>
#include <sys/mem.h>

#include "regs.h"

#define REGISTERS_SIZE	0x1000
#define PORT_COUNT	4
#define SPIN_LIMIT	1000000
#define NAME_SIZE	24

struct pl011 {
	struct console	console;
	uintptr_t	base;
	char		name[NAME_SIZE];
};

static struct pl011 ports[PORT_COUNT];
static unsigned int port_count;

static uint32_t pl011_read(const struct pl011 *port, unsigned int reg)
{
	return *(volatile uint32_t *)(port->base + reg);
}

static void pl011_write(const struct pl011 *port, unsigned int reg, uint32_t value)
{
	*(volatile uint32_t *)(port->base + reg) = value;
}

static bool pl011_wait(const struct pl011 *port, uint32_t busy)
{
	unsigned int spins;

	for (spins = 0; spins < SPIN_LIMIT; spins++) {
		if ((pl011_read(port, UARTFR) & busy) == 0) {
			return true;
		}
	}

	return false;
}

static void pl011_console_write(struct console *console, const char *data, size_t len)
{
	struct pl011 *port = container_of(console, struct pl011, console);

	while (len-- > 0) {
		if (!pl011_wait(port, FR_TXFF)) {
			return;
		}

		pl011_write(port, UARTDR, (uint8_t)*data++);
	}
}

static int pl011_console_read(struct console *console)
{
	struct pl011 *port = container_of(console, struct pl011, console);

	if ((pl011_read(port, UARTFR) & FR_RXFE) != 0) {
		return -1;
	}

	return (int)(pl011_read(port, UARTDR) & DR_DATA);
}

static void pl011_console_flush(struct console *console)
{
	struct pl011 *port = container_of(console, struct pl011, console);

	pl011_wait(port, FR_BUSY);
}

static void pl011_setup(const struct pl011 *port)
{
	if ((pl011_read(port, UARTCR) & CR_UARTEN) != 0) {
		return;
	}

	pl011_write(port, UARTIMSC, 0);
	pl011_write(port, UARTLCR_H, LCR_H_WLEN8 | LCR_H_FEN);
	pl011_write(port, UARTCR, CR_UARTEN | CR_TXE | CR_RXE);
}

static int pl011_attach(uintptr_t base)
{
	struct pl011 *port;
	unsigned int i;
	int err;

	for (i = 0; i < port_count; i++) {
		if (ports[i].base == base) {
			return -EEXIST;
		}
	}

	if (port_count == PORT_COUNT) {
		return -ENOSPC;
	}

	err = mem_map_device(base, REGISTERS_SIZE);
	if (err != 0) {
		return err;
	}

	port = &ports[port_count++];
	port->base = base;
	snprintf(port->name, sizeof(port->name), "pl011@%lx", base);
	port->console.name = port->name;
	port->console.write = pl011_console_write;
	port->console.read = pl011_console_read;
	port->console.flush = pl011_console_flush;

	pl011_setup(port);
	console_register(&port->console);

	return 0;
}

static int pl011_probe(const struct dt *dt, int node)
{
	int console = dt_stdout(dt);
	uint64_t base;
	uint64_t size;
	int err;

	if (console >= 0 && console != node) {
		return -ENODEV;
	}

	err = dt_reg(dt, node, 0, &base, &size);
	if (err != 0) {
		return err;
	}

	return pl011_attach((uintptr_t)base);
}

static const char *const pl011_compatible[] = { "arm,pl011", NULL };

static const struct driver pl011_driver = {
	.name		= "pl011",
	.compatible	= pl011_compatible,
	.probe		= pl011_probe,
	.attach		= pl011_attach,
};

DRIVER(pl011_driver);
