#include <arch.h>
#include <platform.h>
#include <stdio.h>
#include <sys.h>
#include <sys/console.h>
#include <sys/devicetree.h>
#include <sys/driver.h>
#include <sys/mem.h>
#include <sys/random.h>
#include <sys/time.h>

void system_init(void)
{
	printf("cpu el%lu ok\n", boot_el);

	platform_init();
	devicetree_init();
	console_init();
	time_init();
	mem_init();
	random_init();
	driver_init();

	main();
	halt();
}
