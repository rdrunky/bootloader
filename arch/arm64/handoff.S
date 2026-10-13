#include <arch/arm64/asm.h>
#include <arch/arm64/sysreg.h>

	.section .text, "ax"

FUNCTION(arch_handoff)
	msr	daifset, #0xf
	mov	x19, x0
	mov	x20, x1
	address_of x21, handoff_el
	ldr	x21, [x21]

	mrs	x0, sctlr_el1
	bic	x0, x0, #SCTLR_C
	msr	sctlr_el1, x0
	isb
	bl	arch_dcache_clean_invalidate_all

	mrs	x0, sctlr_el1
	ldr	x1, =(SCTLR_M | SCTLR_I)
	bic	x0, x0, x1
	msr	sctlr_el1, x0
	isb
	ic	iallu
	tlbi	vmalle1
	dsb	sy
	isb

	cmp	x21, #2
	b.eq	1f
	mov	x0, x20
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	br	x19

1:	mov	x0, x19
	mov	x1, x20
	hvc	#HVC_HANDOFF
2:	wfe
	b	2b
END(arch_handoff)
