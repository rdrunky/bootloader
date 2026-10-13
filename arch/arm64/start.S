#include <arch/arm64/asm.h>
#include <arch/arm64/sysreg.h>
#include <lib/image.h>

#define RELR_BITMAP_SPAN	(63 * 8)
#define STACK_GUARD_SEED	0x9e3779b97f4a7c15

	.macro	quiesce, el
	mrs	x0, sctlr_el\el
	tst	x0, #SCTLR_C
	b.eq	1f
	bl	arch_dcache_clean_invalidate_all
1:	mrs	x0, sctlr_el\el
	ldr	x1, =(SCTLR_M | SCTLR_C | SCTLR_I)
	bic	x0, x0, x1
	msr	sctlr_el\el, x0
	isb
	ic	iallu
	dsb	nsh
	isb
	.endm

	.section .entry, "ax"

FUNCTION(_start)
	b	.Lentry
	.long	0
	.quad	IMAGE_TEXT_OFFSET
	.quad	__image_end - _start
	.quad	IMAGE_FLAG_PAGE_4K | IMAGE_FLAG_ANYWHERE
	.quad	0
	.quad	0
	.quad	0
	.long	IMAGE_MAGIC
	.long	0

.Lentry:
	msr	daifset, #0xf
	mov	x19, x0
	mov	x22, #1
	mrs	x0, mpidr_el1
	ldr	x1, =MPIDR_AFFINITY_MASK
	tst	x0, x1
	b.ne	.Lpark

	mrs	x20, CurrentEL
	lsr	x20, x20, #CURRENT_EL_SHIFT
	cmp	x20, #3
	b.eq	.Lfrom_el3
	cmp	x20, #2
	b.eq	.Lfrom_el2
	b	.Lat_el1

.Lfrom_el3:
	quiesce	3
	address_of x0, stub_vectors
	msr	vbar_el3, x0
	msr	cptr_el3, xzr
	mrs	x1, id_aa64pfr0_el1
	ubfx	x2, x1, #PFR0_GIC_SHIFT, #PFR0_FIELD_WIDTH
	cbz	x2, 1f
	mov	x2, #(ICC_SRE_SRE | ICC_SRE_ENABLE)
	msr	icc_sre_el3, x2
	isb
1:	ubfx	x2, x1, #PFR0_EL2_SHIFT, #PFR0_FIELD_WIDTH
	cbz	x2, 2f
	ldr	x0, =(SCR_RES1 | SCR_NS | SCR_HCE | SCR_RW)
	msr	scr_el3, x0
	ldr	x0, =SCTLR_EL2_RES1
	msr	sctlr_el2, x0
	mov	x0, #(SPSR_DAIF | SPSR_EL2H)
	adr	x1, .Lfrom_el2
	b	3f
2:	ldr	x0, =(SCR_RES1 | SCR_NS | SCR_RW)
	msr	scr_el3, x0
	ldr	x0, =SCTLR_EL1_RES1
	msr	sctlr_el1, x0
	mov	x0, #(SPSR_DAIF | SPSR_EL1H)
	adr	x1, .Lat_el1
3:	msr	spsr_el3, x0
	msr	elr_el3, x1
	isb
	eret

.Lfrom_el2:
	quiesce	2
	mov	x22, #2
	address_of x0, stub_vectors
	msr	vbar_el2, x0
	mov	x0, #HCR_RW
	msr	hcr_el2, x0
	ldr	x0, =CPTR_EL2_RES1
	msr	cptr_el2, x0
	msr	hstr_el2, xzr
	msr	vttbr_el2, xzr
	mrs	x0, cnthctl_el2
	orr	x0, x0, #(CNTHCTL_EL1PCTEN | CNTHCTL_EL1PCEN)
	msr	cnthctl_el2, x0
	msr	cntvoff_el2, xzr
	mrs	x0, midr_el1
	msr	vpidr_el2, x0
	mrs	x0, mpidr_el1
	msr	vmpidr_el2, x0
	ldr	x0, =SCTLR_EL1_RES1
	msr	sctlr_el1, x0
	mov	x0, #(SPSR_DAIF | SPSR_EL1H)
	msr	spsr_el2, x0
	adr	x0, .Lat_el1
	msr	elr_el2, x0
	isb
	eret

.Lat_el1:
	quiesce	1
	ldr	x0, =SCTLR_EL1_RES1
	msr	sctlr_el1, x0
	isb

	adr	x21, _start
	address_of x0, __relr_start
	address_of x1, __relr_end
	mov	x5, xzr
.Lrelocate:
	cmp	x0, x1
	b.hs	.Lclear_bss
	ldr	x2, [x0], #8
	tbnz	x2, #0, .Lbitmap
	add	x5, x2, x21
	ldr	x3, [x5]
	add	x3, x3, x21
	str	x3, [x5], #8
	b	.Lrelocate

.Lbitmap:
	mov	x6, x5
	lsr	x2, x2, #1
.Lbitmap_bit:
	cbz	x2, .Lbitmap_done
	tbz	x2, #0, .Lbitmap_next
	ldr	x3, [x6]
	add	x3, x3, x21
	str	x3, [x6]
.Lbitmap_next:
	add	x6, x6, #8
	lsr	x2, x2, #1
	b	.Lbitmap_bit
.Lbitmap_done:
	add	x5, x5, #RELR_BITMAP_SPAN
	b	.Lrelocate

.Lclear_bss:
	address_of x0, __bss_start
	address_of x1, __bss_end
1:	cmp	x0, x1
	b.hs	2f
	stp	xzr, xzr, [x0], #16
	b	1b

2:	msr	spsel, #1
	address_of x0, __exception_stack_top
	mov	sp, x0
	msr	spsel, #0
	address_of x0, __stack_top
	mov	sp, x0

	address_of x0, exception_vectors
	msr	vbar_el1, x0
	isb

	mrs	x0, cntpct_el0
	ldr	x1, =STACK_GUARD_SEED
	mul	x0, x0, x1
	eor	x0, x0, x1, ror #29
	bic	x0, x0, #0xff
	address_of x1, __stack_chk_guard
	str	x0, [x1]

	address_of x0, boot_arg
	str	x19, [x0]
	address_of x0, boot_el
	str	x20, [x0]
	address_of x0, handoff_el
	str	x22, [x0]

	mov	x29, xzr
	mov	x30, xzr
	bl	system_init

.Lpark:
	wfe
	b	.Lpark
END(_start)
