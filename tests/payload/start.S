#include <lib/image.h>

#define RELR_BITMAP_SPAN	(63 * 8)

	.section .entry, "ax"
	.globl	_start
_start:
	b	entry
	.long	0
	.quad	0
	.quad	__payload_end - _start
	.quad	IMAGE_FLAG_PAGE_4K | IMAGE_FLAG_ANYWHERE
	.quad	0
	.quad	0
	.quad	0
	.long	IMAGE_MAGIC
	.long	0

entry:
	mov	x19, x0
	mov	x20, x1
	mov	x21, x2
	mov	x22, x3
	adr	x23, _start

	adrp	x0, __relr_start
	add	x0, x0, :lo12:__relr_start
	adrp	x1, __relr_end
	add	x1, x1, :lo12:__relr_end
	mov	x5, xzr
relocate:
	cmp	x0, x1
	b.hs	clear_bss
	ldr	x2, [x0], #8
	tbnz	x2, #0, bitmap
	add	x5, x2, x23
	ldr	x3, [x5]
	add	x3, x3, x23
	str	x3, [x5], #8
	b	relocate

bitmap:
	mov	x6, x5
	lsr	x2, x2, #1
bitmap_bit:
	cbz	x2, bitmap_done
	tbz	x2, #0, bitmap_next
	ldr	x3, [x6]
	add	x3, x3, x23
	str	x3, [x6]
bitmap_next:
	add	x6, x6, #8
	lsr	x2, x2, #1
	b	bitmap_bit
bitmap_done:
	add	x5, x5, #RELR_BITMAP_SPAN
	b	relocate

clear_bss:
	adrp	x0, __bss_start
	add	x0, x0, :lo12:__bss_start
	adrp	x1, __bss_end
	add	x1, x1, :lo12:__bss_end
clear_word:
	cmp	x0, x1
	b.hs	run
	str	xzr, [x0], #8
	b	clear_word

run:
	adrp	x0, __stack_top
	add	x0, x0, :lo12:__stack_top
	mov	sp, x0
	mov	x0, x19
	mov	x1, x20
	mov	x2, x21
	mov	x3, x22
	mrs	x4, CurrentEL
	lsr	x4, x4, #2
	bl	payload_main

park:
	wfi
	b	park
