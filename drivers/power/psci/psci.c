#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/driver.h>
#include <sys/power.h>

#define PSCI_VERSION		0x84000000u
#define PSCI_SYSTEM_OFF		0x84000008u
#define PSCI_SYSTEM_RESET	0x84000009u

enum conduit {
	CONDUIT_HVC,
	CONDUIT_SMC,
};

static enum conduit conduit;

static int64_t psci_call(uint32_t function)
{
	register uint64_t x0 __asm__("x0") = function;
	register uint64_t x1 __asm__("x1") = 0;
	register uint64_t x2 __asm__("x2") = 0;
	register uint64_t x3 __asm__("x3") = 0;

	if (conduit == CONDUIT_HVC) {
		__asm__ volatile("hvc #0"
				 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
				 :
				 : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
				   "x12", "x13", "x14", "x15", "x16", "x17", "memory");
	} else {
		__asm__ volatile("smc #0"
				 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
				 :
				 : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
				   "x12", "x13", "x14", "x15", "x16", "x17", "memory");
	}

	return (int32_t)x0;
}

static void psci_reset(void)
{
	psci_call(PSCI_SYSTEM_RESET);
}

static void psci_poweroff(void)
{
	psci_call(PSCI_SYSTEM_OFF);
}

static const struct power_ops psci_power = {
	.name		= "psci",
	.reset		= psci_reset,
	.poweroff	= psci_poweroff,
};

static int psci_probe(const struct dt *dt, int node)
{
	const char *method = dt_read_string(dt, node, "method");

	if (method == NULL) {
		return -EINVAL;
	}

	if (strcmp(method, "hvc") == 0) {
		conduit = CONDUIT_HVC;
	} else if (strcmp(method, "smc") == 0) {
		conduit = CONDUIT_SMC;
	} else {
		return -ENOTSUP;
	}

	if (psci_call(PSCI_VERSION) < 0) {
		return -ENODEV;
	}

	power_register(&psci_power);

	return 0;
}

static const char *const psci_compatible[] = { "arm,psci-1.0", "arm,psci-0.2", NULL };

static const struct driver psci_driver = {
	.name		= "psci",
	.compatible	= psci_compatible,
	.probe		= psci_probe,
};

DRIVER(psci_driver);
