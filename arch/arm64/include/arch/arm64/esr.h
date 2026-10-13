#ifndef ARCH_ARM64_ESR_H
#define ARCH_ARM64_ESR_H

#include <sys/bits.h>

#define ESR_EC_SHIFT		26
#define ESR_EC_WIDTH		6
#define ESR_ISS_MASK		UL(0x1ffffff)
#define ESR_IMM16_MASK		UL(0xffff)

#define ESR_EC(esr)		(((esr) >> ESR_EC_SHIFT) & GENMASK(ESR_EC_WIDTH - 1, 0))
#define ESR_ISS(esr)		((esr) & ESR_ISS_MASK)

#define ESR_EC_UNKNOWN		0x00
#define ESR_EC_WFX		0x01
#define ESR_EC_ILLEGAL_STATE	0x0e
#define ESR_EC_SVC64		0x15
#define ESR_EC_HVC64		0x16
#define ESR_EC_SMC64		0x17
#define ESR_EC_SYSREG		0x18
#define ESR_EC_IABT_LOWER	0x20
#define ESR_EC_IABT_CURRENT	0x21
#define ESR_EC_PC_ALIGN		0x22
#define ESR_EC_DABT_LOWER	0x24
#define ESR_EC_DABT_CURRENT	0x25
#define ESR_EC_SP_ALIGN		0x26
#define ESR_EC_SERROR		0x2f
#define ESR_EC_BRK64		0x3c

#define ESR_FSC_MASK		0x3f
#define ESR_FSC_TYPE_MASK	0x3c
#define ESR_FSC_LEVEL_MASK	0x03

#define ESR_ISS_FSC(iss)	((iss) & ESR_FSC_MASK)
#define ESR_ISS_WNR		BIT(6)

#endif
