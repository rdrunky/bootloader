#include <platform.h>
#include <platform/memmap.h>
#include <sys/driver.h>

void platform_init(void)
{
	driver_attach("pl011", UART0_BASE);
}

uintptr_t platform_devicetree(void)
{
	return RAM_BASE;
}

void platform_memory(uint64_t *base, uint64_t *size)
{
	*base = RAM_BASE;
	*size = RAM_SIZE_MIN;
}
