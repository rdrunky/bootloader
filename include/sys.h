#ifndef SYS_H
#define SYS_H

#include <compiler.h>

int main(void);

void system_init(void);
void halt(void) __noreturn;

#endif
