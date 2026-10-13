#include <stdio.h>
#include <sys.h>
#include <sys/console.h>
#include <sys/power.h>

static const struct power_ops *power;

void power_register(const struct power_ops *ops)
{
	power = ops;
	printf("power %s ok\n", ops->name);
}

void system_reset(void)
{
	console_flush();

	if (power != NULL && power->reset != NULL) {
		power->reset();
	}

	printf("reset unavailable, halting\n");
	halt();
}

void system_poweroff(void)
{
	console_flush();

	if (power != NULL && power->poweroff != NULL) {
		power->poweroff();
	}

	printf("power off unavailable, halting\n");
	halt();
}
