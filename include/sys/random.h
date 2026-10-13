#ifndef SYS_RANDOM_H
#define SYS_RANDOM_H

#include <stddef.h>

#define RANDOM_READ_MAX		64

struct random_source {
	const char		*name;
	int			(*read)(struct random_source *source, void *buffer, size_t size);
	struct random_source	*next;
};

void random_init(void);
void random_register(struct random_source *source);
int random_read(void *buffer, size_t size);

#endif
