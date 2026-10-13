#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/devicetree.h>
#include <sys/driver.h>

#define QUIESCE_MAX	32

struct quiesce {
	void	(*function)(void *context);
	void	*context;
};

SET_DECLARE(drivers, const struct driver);

static struct quiesce quiesces[QUIESCE_MAX];
static size_t quiesce_count;

static const struct driver *driver_find(const char *name)
{
	const struct driver *driver;

	SET_FOREACH(driver, drivers) {
		if (strcmp(driver->name, name) == 0) {
			return driver;
		}
	}

	return NULL;
}

static bool driver_matches(const struct driver *driver, const struct dt *dt, int node)
{
	const char *const *compatible;

	for (compatible = driver->compatible; compatible != NULL && *compatible != NULL; compatible++) {
		if (dt_compatible(dt, node, *compatible)) {
			return true;
		}
	}

	return false;
}

static const struct driver *driver_match(const struct dt *dt, int node)
{
	const struct driver *driver;

	SET_FOREACH(driver, drivers) {
		if (driver->probe != NULL && driver_matches(driver, dt, node)) {
			return driver;
		}
	}

	return NULL;
}

int driver_attach(const char *name, uintptr_t base)
{
	const struct driver *driver = driver_find(name);

	if (driver == NULL || driver->attach == NULL) {
		return -ENODEV;
	}

	return driver->attach(base);
}

int driver_probe(const struct dt *dt, int node)
{
	const struct driver *driver = driver_match(dt, node);

	if (driver == NULL || !dt_enabled(dt, node)) {
		return -ENODEV;
	}

	return driver->probe(dt, node);
}

int driver_quiesce_register(void (*function)(void *context), void *context)
{
	if (quiesce_count == QUIESCE_MAX) {
		return -ENOSPC;
	}

	quiesces[quiesce_count].function = function;
	quiesces[quiesce_count].context = context;
	quiesce_count++;

	return 0;
}

void driver_quiesce_unregister(void (*function)(void *context), void *context)
{
	size_t i = quiesce_count;

	while (i > 0) {
		i--;
		if (quiesces[i].function == function && quiesces[i].context == context) {
			memmove(&quiesces[i], &quiesces[i + 1], (quiesce_count - i - 1) * sizeof(quiesces[0]));
			quiesce_count--;
			return;
		}
	}
}

void driver_quiesce(void)
{
	while (quiesce_count > 0) {
		const struct quiesce *hook = &quiesces[--quiesce_count];

		hook->function(hook->context);
	}
}

void driver_init(void)
{
	const struct dt *dt = devicetree();
	int node;

	if (dt == NULL) {
		return;
	}

	for (node = dt_root(dt); node >= 0; node = dt_next_node(dt, node, NULL)) {
		const struct driver *driver = driver_match(dt, node);
		int err;

		if (driver == NULL || !dt_enabled(dt, node)) {
			continue;
		}

		err = driver->probe(dt, node);
		if (err != 0 && err != -EEXIST && err != -ENODEV) {
			printf("%s %s %s\n", driver->name, dt_name(dt, node), strerror(-err));
		}
	}
}
