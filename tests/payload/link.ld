OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

STACK_SIZE = 0x4000;

SECTIONS
{
	. = 0;

	.text : {
		KEEP(*(.entry))
		*(.text .text.*)
	}

	.rodata : {
		*(.rodata .rodata.*)
	}

	.data.rel.ro : ALIGN(8) {
		*(.data.rel.ro .data.rel.ro.*)
		*(.got)
	}

	.relr.dyn : ALIGN(8) {
		__relr_start = .;
		*(.relr.dyn)
		__relr_end = .;
	}

	.rela.dyn : {
		*(.rela .rela.*)
	}

	.data : ALIGN(8) {
		*(.data .data.*)
	}

	.bss (NOLOAD) : ALIGN(16) {
		__bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(16);
		__bss_end = .;
		. += STACK_SIZE;
		__stack_top = .;
	}

	__payload_end = .;

	/DISCARD/ : {
		*(.interp .dynamic .dynsym .dynstr .hash .gnu.hash)
		*(.comment .note .note.*)
		*(.eh_frame .eh_frame_hdr)
	}
}
