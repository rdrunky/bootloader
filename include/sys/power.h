#ifndef SYS_POWER_H
#define SYS_POWER_H

#include <compiler.h>

struct power_ops {
	const char	*name;
	void		(*reset)(void);
	void		(*poweroff)(void);
};

void power_register(const struct power_ops *ops);

void system_reset(void) __noreturn;
void system_poweroff(void) __noreturn;

#endif
