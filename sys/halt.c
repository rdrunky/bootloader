#include <arch.h>
#include <sys.h>

void halt(void)
{
	arch_irq_disable();

	for (;;) {
		arch_idle();
	}
}
