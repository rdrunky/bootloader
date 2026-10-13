#include <arch/arm64/asm.h>
#include <arch/arm64/sysreg.h>

#define CCSIDR_LINE_MASK	7
#define CCSIDR_LINE_BIAS	4
#define CCSIDR_WAYS_SHIFT	3
#define CCSIDR_WAYS_WIDTH	10
#define CCSIDR_SETS_SHIFT	13
#define CCSIDR_SETS_WIDTH	15
#define CCSIDR_WIDE_WAYS_WIDTH	21
#define CCSIDR_WIDE_SETS_SHIFT	32
#define CCSIDR_WIDE_SETS_WIDTH	24
#define MMFR2_CCIDX_SHIFT	20
#define MMFR2_CCIDX_WIDTH	4

	.macro	dcache_range, op
	add	x1, x0, x1
	mrs	x3, ctr_el0
	ubfx	x3, x3, #CTR_DMINLINE_SHIFT, #CTR_DMINLINE_WIDTH
	mov	x2, #4
	lsl	x2, x2, x3
	sub	x3, x2, #1
	bic	x0, x0, x3
1:	cmp	x0, x1
	b.hs	2f
	dc	\op, x0
	add	x0, x0, x2
	b	1b
2:	dsb	sy
	ret
	.endm

	.section .text, "ax"

FUNCTION(arch_dcache_clean_range)
	dcache_range cvac
END(arch_dcache_clean_range)

FUNCTION(arch_dcache_clean_invalidate_range)
	dcache_range civac
END(arch_dcache_clean_invalidate_range)

FUNCTION(arch_dcache_invalidate_range)
	dcache_range ivac
END(arch_dcache_invalidate_range)

FUNCTION(arch_dcache_clean_invalidate_all)
	dsb	sy
	mrs	x0, clidr_el1
	ubfx	x1, x0, #CLIDR_LOC_SHIFT, #CLIDR_LOC_WIDTH
	cbz	x1, .Ldone
	lsl	x1, x1, #1
	mrs	x2, id_aa64mmfr2_el1
	ubfx	x2, x2, #MMFR2_CCIDX_SHIFT, #MMFR2_CCIDX_WIDTH
	mov	x3, xzr

.Llevel:
	add	x4, x3, x3, lsr #1
	lsr	x4, x0, x4
	and	x4, x4, #7
	cmp	x4, #CLIDR_CTYPE_DATA
	b.lt	.Lnext_level

	msr	csselr_el1, x3
	isb
	mrs	x5, ccsidr_el1
	and	x6, x5, #CCSIDR_LINE_MASK
	add	x6, x6, #CCSIDR_LINE_BIAS
	cbnz	x2, .Lwide
	ubfx	x7, x5, #CCSIDR_WAYS_SHIFT, #CCSIDR_WAYS_WIDTH
	ubfx	x8, x5, #CCSIDR_SETS_SHIFT, #CCSIDR_SETS_WIDTH
	b	.Lgeometry

.Lwide:
	ubfx	x7, x5, #CCSIDR_WAYS_SHIFT, #CCSIDR_WIDE_WAYS_WIDTH
	ubfx	x8, x5, #CCSIDR_WIDE_SETS_SHIFT, #CCSIDR_WIDE_SETS_WIDTH

.Lgeometry:
	clz	w9, w7

.Lset:
	mov	x10, x7

.Lway:
	lsl	x11, x10, x9
	orr	x11, x11, x3
	lsl	x12, x8, x6
	orr	x11, x11, x12
	dc	cisw, x11
	subs	x10, x10, #1
	b.ge	.Lway
	subs	x8, x8, #1
	b.ge	.Lset

.Lnext_level:
	add	x3, x3, #2
	cmp	x3, x1
	b.lt	.Llevel

.Ldone:
	msr	csselr_el1, xzr
	dsb	sy
	isb
	ret
END(arch_dcache_clean_invalidate_all)

FUNCTION(arch_memory_barrier)
	dsb	sy
	ret
END(arch_memory_barrier)
