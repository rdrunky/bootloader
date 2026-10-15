#ifndef DRIVERS_RAMFB_H
#define DRIVERS_RAMFB_H

#include <stddef.h>

int ramfb_init(void);
int ramfb_set_mode(size_t width, size_t height, size_t scale);
int ramfb_get_mode(size_t *width, size_t *height, size_t *scale);

#endif
