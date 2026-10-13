#ifndef SYS_DRIVER_H
#define SYS_DRIVER_H

#include <lib/devicetree.h>
#include <stdint.h>
#include <sys/linkerset.h>

struct driver {
	const char		*name;
	const char *const	*compatible;
	int			(*probe)(const struct dt *dt, int node);
	int			(*attach)(uintptr_t base);
};

#define DRIVER(driver)	SET_ENTRY(drivers, driver)

void driver_init(void);
int driver_attach(const char *name, uintptr_t base);
int driver_probe(const struct dt *dt, int node);

int driver_quiesce_register(void (*quiesce)(void *context), void *context);
void driver_quiesce_unregister(void (*quiesce)(void *context), void *context);
void driver_quiesce(void);

#endif
