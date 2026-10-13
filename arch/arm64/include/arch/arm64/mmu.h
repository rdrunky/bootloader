#ifndef ARCH_ARM64_MMU_H
#define ARCH_ARM64_MMU_H

#include <sys/bits.h>

#define PTE_VALID		BIT(0)
#define PTE_TABLE		BIT(1)
#define PTE_PAGE		BIT(1)
#define PTE_ATTR_INDEX(index)	(UL(index) << 2)
#define PTE_READONLY		BIT(7)
#define PTE_INNER_SHAREABLE	(UL(3) << 8)
#define PTE_ACCESSED		BIT(10)
#define PTE_PXN			BIT(53)
#define PTE_UXN			BIT(54)
#define PTE_ADDRESS		GENMASK(47, 12)

#define ATTR_DEVICE		0
#define ATTR_NORMAL		1
#define ATTR_NONCACHEABLE	2

#define MAIR_VALUE		(UL(0x00) << (8 * ATTR_DEVICE) | \
				 UL(0xff) << (8 * ATTR_NORMAL) | \
				 UL(0x44) << (8 * ATTR_NONCACHEABLE))

#define TCR_T0SZ(bits)		(UL(64) - (bits))
#define TCR_IRGN0_WBWA		(UL(1) << 8)
#define TCR_ORGN0_WBWA		(UL(1) << 10)
#define TCR_SH0_INNER		(UL(3) << 12)
#define TCR_TG0_4K		(UL(0) << 14)
#define TCR_EPD1		BIT(23)
#define TCR_IPS_SHIFT		32

#define MMFR0_PARANGE_MASK	UL(0xf)
#define PARANGE_48_BITS		UL(5)

#endif
