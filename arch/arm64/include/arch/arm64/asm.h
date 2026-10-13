#ifndef ARCH_ARM64_ASM_H
#define ARCH_ARM64_ASM_H

#define HVC_HANDOFF		1

#define FUNCTION(name)			\
	.globl	name;			\
	.type	name, %function;	\
name:

#define END(name)			\
	.size	name, . - name

	.macro	address_of, reg, symbol
	adrp	\reg, \symbol
	add	\reg, \reg, :lo12:\symbol
	.endm

#endif
