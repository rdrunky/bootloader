#ifndef DEBUG_H
#define DEBUG_H

#include <compiler.h>
#include <stdint.h>

extern uintptr_t __stack_chk_guard;

void panic(const char *fmt, ...) __printflike(1, 2) __noreturn;
void __stack_chk_fail(void) __noreturn;

#endif
