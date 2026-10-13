#include <arch/arm64/asm.h>
#include <arch/arm64/esr.h>
#include <arch/arm64/sysreg.h>

#define CURRENT_EL3		(3 << CURRENT_EL_SHIFT)

	.macro	vector, target
	.balign	0x80
	b	\target
	.endm

	.section .text, "ax"

	.balign	0x800
FUNCTION(stub_vectors)
	vector	stub_park
	vector	stub_park
	vector	stub_park
	vector	stub_park
	vector	stub_park
	vector	stub_park
	vector	stub_park
	vector	stub_park
	vector	stub_sync
	vector	stub_park
	vector	stub_park
	vector	stub_park
	vector	stub_park
	vector	stub_park
	vector	stub_park
	vector	stub_park
END(stub_vectors)

stub_sync:
	mrs	x9, CurrentEL
	cmp	x9, #CURRENT_EL3
	b.eq	monitor_sync

hyp_sync:
	mrs	x9, esr_el2
	ubfx	x10, x9, #ESR_EC_SHIFT, #ESR_EC_WIDTH
	cmp	x10, #ESR_EC_HVC64
	b.ne	stub_park
	and	x9, x9, #ESR_IMM16_MASK
	cmp	x9, #HVC_HANDOFF
	b.ne	unsupported
	mov	x9, x0
	mov	x0, x1
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	br	x9

monitor_sync:
	mrs	x9, esr_el3
	ubfx	x10, x9, #ESR_EC_SHIFT, #ESR_EC_WIDTH
	cmp	x10, #ESR_EC_SMC64
	b.ne	stub_park

unsupported:
	mov	x0, #-1
	eret

stub_park:
	wfe
	b	stub_park
