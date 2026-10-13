#include <arch.h>
#include <debug.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys.h>
#include <sys/console.h>

uintptr_t __stack_chk_guard;

void panic(const char *fmt, ...)
{
	static bool panicking;
	va_list ap;

	arch_irq_disable();

	if (panicking) {
		halt();
	}
	panicking = true;

	printf("\npanic: ");

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	printf("\nbacktrace:\n");
	arch_backtrace((uintptr_t)__builtin_frame_address(0));
	printf("halted\n");

	console_flush();
	halt();
}

void __stack_chk_fail(void)
{
	panic("stack smashing detected");
}
