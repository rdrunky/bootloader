#ifndef DRIVERS_QEMU_RAMFB_FONT_H
#define DRIVERS_QEMU_RAMFB_FONT_H

#include <stdint.h>

#define FONT_WIDTH	8
#define FONT_HEIGHT	8

extern const uint8_t font8x8[128][FONT_HEIGHT];

#endif
