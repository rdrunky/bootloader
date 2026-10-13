#include <arch.h>
#include <arch/arm64/sysreg.h>

uint64_t arch_timer_frequency(void)
{
	return read_sysreg(cntfrq_el0);
}

uint64_t arch_timer_count(void)
{
	isb();

	return read_sysreg(cntpct_el0);
}
