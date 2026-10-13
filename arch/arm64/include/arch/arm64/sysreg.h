#ifndef ARCH_ARM64_SYSREG_H
#define ARCH_ARM64_SYSREG_H

#include <sys/bits.h>

#define CURRENT_EL_SHIFT	2

#define MPIDR_AFFINITY_MASK	UL(0xff00ffffff)

#define PFR0_EL2_SHIFT		8
#define PFR0_GIC_SHIFT		24
#define PFR0_FIELD_WIDTH	4

#define ISAR0_RNDR_SHIFT	60
#define ISAR0_FIELD_MASK	UL(0xf)

#define NZCV_Z			BIT(30)

#define SCTLR_M			BIT(0)
#define SCTLR_C			BIT(2)
#define SCTLR_SA		BIT(3)
#define SCTLR_I			BIT(12)
#define SCTLR_WXN		BIT(19)
#define SCTLR_EL1_RES1		UL(0x30d00800)
#define SCTLR_EL2_RES1		UL(0x30c50830)

#define SCR_NS			BIT(0)
#define SCR_RES1		(BIT(4) | BIT(5))
#define SCR_HCE			BIT(8)
#define SCR_RW			BIT(10)

#define HCR_RW			BIT(31)
#define CPTR_EL2_RES1		UL(0x33ff)

#define CNTHCTL_EL1PCTEN	BIT(0)
#define CNTHCTL_EL1PCEN		BIT(1)

#define ICC_SRE_SRE		BIT(0)
#define ICC_SRE_ENABLE		BIT(3)

#define SPSR_EL1H		UL(0x5)
#define SPSR_EL2H		UL(0x9)
#define SPSR_DAIF		UL(0x3c0)

#define CLIDR_LOC_SHIFT		24
#define CLIDR_LOC_WIDTH		3
#define CLIDR_CTYPE_DATA	2

#define CTR_DMINLINE_SHIFT	16
#define CTR_DMINLINE_WIDTH	4

#ifndef __ASSEMBLER__

#include <stdint.h>

#define read_sysreg(reg) ({						\
	uint64_t _sysreg_read;						\
	__asm__ volatile("mrs %0, " #reg : "=r"(_sysreg_read));	\
	_sysreg_read;							\
})

#define write_sysreg(reg, value) do {					\
	uint64_t _sysreg_write = (value);				\
	__asm__ volatile("msr " #reg ", %0" :: "r"(_sysreg_write));	\
} while (0)

static inline void isb(void)
{
	__asm__ volatile("isb" ::: "memory");
}

#endif

#endif
