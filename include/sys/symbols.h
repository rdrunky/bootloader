#ifndef SYS_SYMBOLS_H
#define SYS_SYMBOLS_H

#include <stdint.h>

const char *symbol_lookup(uintptr_t address, uintptr_t *offset);

#endif
