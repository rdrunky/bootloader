#include <arch/arm64/asm.h>
#include <errno.h>

	.section .text, "ax"

FUNCTION(arch_probe_read)
	address_of x9, trap_fixup
	adr	x10, .Lread_fault
	str	x10, [x9]
	cmp	w1, #1
	b.eq	.Lread_8
	cmp	w1, #2
	b.eq	.Lread_16
	cmp	w1, #4
	b.eq	.Lread_32
	cmp	w1, #8
	b.eq	.Lread_64
	str	xzr, [x9]
	mov	w0, #-EINVAL
	ret
.Lread_8:
	ldrb	w11, [x0]
	b	.Lread_done
.Lread_16:
	ldrh	w11, [x0]
	b	.Lread_done
.Lread_32:
	ldr	w11, [x0]
	b	.Lread_done
.Lread_64:
	ldr	x11, [x0]
.Lread_done:
	str	xzr, [x9]
	str	x11, [x2]
	mov	w0, wzr
	ret
.Lread_fault:
	str	xzr, [x9]
	mov	w0, #-EFAULT
	ret
END(arch_probe_read)

FUNCTION(arch_probe_write)
	address_of x9, trap_fixup
	adr	x10, .Lwrite_fault
	str	x10, [x9]
	cmp	w1, #1
	b.eq	.Lwrite_8
	cmp	w1, #2
	b.eq	.Lwrite_16
	cmp	w1, #4
	b.eq	.Lwrite_32
	cmp	w1, #8
	b.eq	.Lwrite_64
	str	xzr, [x9]
	mov	w0, #-EINVAL
	ret
.Lwrite_8:
	strb	w2, [x0]
	b	.Lwrite_done
.Lwrite_16:
	strh	w2, [x0]
	b	.Lwrite_done
.Lwrite_32:
	str	w2, [x0]
	b	.Lwrite_done
.Lwrite_64:
	str	x2, [x0]
.Lwrite_done:
	str	xzr, [x9]
	mov	w0, wzr
	ret
.Lwrite_fault:
	str	xzr, [x9]
	mov	w0, #-EFAULT
	ret
END(arch_probe_write)
