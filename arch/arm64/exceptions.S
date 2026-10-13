#include <arch/arm64/asm.h>
#include <arch/arm64/trap.h>

	.macro	vector, index
	.balign	0x80
	sub	sp, sp, #TRAP_FRAME_SIZE
	stp	x0, x1, [sp, #TRAP_X0]
	mov	x0, #\index
	b	trap_entry
	.endm

	.section .text, "ax"

	.balign	0x800
FUNCTION(exception_vectors)
	vector	0
	vector	1
	vector	2
	vector	3
	vector	4
	vector	5
	vector	6
	vector	7
	vector	8
	vector	9
	vector	10
	vector	11
	vector	12
	vector	13
	vector	14
	vector	15
END(exception_vectors)

trap_entry:
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	stp	x28, x29, [sp, #224]
	str	x30, [sp, #TRAP_X30]
	str	x0, [sp, #TRAP_VECTOR]

	mrs	x1, sp_el0
	add	x2, sp, #TRAP_FRAME_SIZE
	sub	x3, x0, #(VECTOR_GROUP_NESTED * VECTOR_GROUP_SIZE)
	cmp	x3, #VECTOR_GROUP_SIZE
	csel	x1, x2, x1, lo
	str	x1, [sp, #TRAP_SP]

	mrs	x1, elr_el1
	mrs	x2, spsr_el1
	stp	x1, x2, [sp, #TRAP_ELR]
	mrs	x1, esr_el1
	mrs	x2, far_el1
	stp	x1, x2, [sp, #TRAP_ESR]

	mov	x0, sp
	mov	x29, xzr
	bl	trap_handler

	ldp	x1, x2, [sp, #TRAP_ELR]
	msr	elr_el1, x1
	msr	spsr_el1, x2
	ldp	x0, x1, [sp, #TRAP_X0]
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	x8, x9, [sp, #64]
	ldp	x10, x11, [sp, #80]
	ldp	x12, x13, [sp, #96]
	ldp	x14, x15, [sp, #112]
	ldp	x16, x17, [sp, #128]
	ldp	x18, x19, [sp, #144]
	ldp	x20, x21, [sp, #160]
	ldp	x22, x23, [sp, #176]
	ldp	x24, x25, [sp, #192]
	ldp	x26, x27, [sp, #208]
	ldp	x28, x29, [sp, #224]
	ldr	x30, [sp, #TRAP_X30]
	add	sp, sp, #TRAP_FRAME_SIZE
	eret
