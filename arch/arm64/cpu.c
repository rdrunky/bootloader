#include <arch.h>
#include <arch/arm64/sysreg.h>
#include <errno.h>

extern char __image_start[];
extern char __image_end[];

uint64_t boot_el;
uint64_t boot_arg;
uint64_t handoff_el;

uintptr_t arch_image_start(void)
{
	return (uintptr_t)__image_start;
}

size_t arch_image_size(void)
{
	return (size_t)(__image_end - __image_start);
}

int arch_random(uint64_t *value)
{
	uint64_t flags;

	if (((read_sysreg(id_aa64isar0_el1) >> ISAR0_RNDR_SHIFT) & ISAR0_FIELD_MASK) == 0) {
		return -ENOTSUP;
	}

	__asm__ volatile("mrs %0, s3_3_c2_c4_0\n\tmrs %1, nzcv" : "=r"(*value), "=r"(flags));

	return (flags & NZCV_Z) != 0 ? -EAGAIN : 0;
}

void arch_idle(void)
{
	__asm__ volatile("wfi");
}

void arch_irq_disable(void)
{
	__asm__ volatile("msr daifset, #0xf" ::: "memory");
}
