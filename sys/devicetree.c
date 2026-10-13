#include <arch.h>
#include <errno.h>
#include <platform.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/devicetree.h>

static struct dt boot_dt;
static bool present;

static int open_at(uintptr_t address)
{
	uint64_t magic;

	if (address == 0) {
		return -ENOENT;
	}

	if (arch_probe_read(address, sizeof(uint32_t), &magic) != 0) {
		return -EFAULT;
	}

	return dt_open(&boot_dt, (const void *)address, DT_SIZE_MAX);
}

void devicetree_init(void)
{
	uintptr_t fallback = platform_devicetree();
	int err = open_at(boot_arg);

	if (err != 0 && boot_arg != 0) {
		printf("dt %#lx %s\n", boot_arg, strerror(-err));
	}

	if (err != 0 && fallback != boot_arg) {
		err = open_at(fallback);
	}

	if (err != 0) {
		printf("dt missing\n");
		return;
	}

	present = true;
}

const struct dt *devicetree(void)
{
	return present ? &boot_dt : NULL;
}
