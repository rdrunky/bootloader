#ifndef SYS_TIME_H
#define SYS_TIME_H

#include <stdint.h>

#define USEC_PER_MSEC	UINT64_C(1000)
#define USEC_PER_SEC	UINT64_C(1000000)

void time_init(void);
uint64_t time_us(void);

#endif
