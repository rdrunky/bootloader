OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

PAGE_SIZE = 0x1000;
STACK_SIZE = 0x10000;
EXCEPTION_STACK_SIZE = 0x4000;

SECTIONS
{
	. = 0;
	__image_start = .;

	.text : {
		KEEP(*(.entry))
		*(.text .text.*)
	}

	. = ALIGN(PAGE_SIZE);
	__text_end = .;

	.rodata : {
		*(.rodata .rodata.*)
		. = ALIGN(4);
		KEEP(*(.symbols))
	}

	.data.rel.ro : ALIGN(8) {
		*(.data.rel.ro .data.rel.ro.*)
		*(.got .got.plt)
		. = ALIGN(8);
		__set_drivers_start = .;
		KEEP(*(.set.drivers))
		__set_drivers_stop = .;
		__set_commands_start = .;
		KEEP(*(.set.commands))
		__set_commands_stop = .;
		__set_virtio_drivers_start = .;
		KEEP(*(.set.virtio_drivers))
		__set_virtio_drivers_stop = .;
		__set_pci_drivers_start = .;
		KEEP(*(.set.pci_drivers))
		__set_pci_drivers_stop = .;
	}

	.relr.dyn : ALIGN(8) {
		__relr_start = .;
		*(.relr.dyn)
		__relr_end = .;
	}

	.rela.dyn : {
		*(.rela .rela.*)
	}

	. = ALIGN(PAGE_SIZE);
	__rodata_end = .;

	.data : {
		*(.data .data.*)
	}

	.bss (NOLOAD) : ALIGN(16) {
		__bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(16);
		__bss_end = .;
	}

	.stack (NOLOAD) : ALIGN(PAGE_SIZE) {
		__stack_guard = .;
		. += PAGE_SIZE;
		__stack_bottom = .;
		. += STACK_SIZE;
		__stack_top = .;
		__exception_stack_guard = .;
		. += PAGE_SIZE;
		__exception_stack_bottom = .;
		. += EXCEPTION_STACK_SIZE;
		__exception_stack_top = .;
	}

	. = ALIGN(PAGE_SIZE);
	__image_end = .;

	/DISCARD/ : {
		*(.interp .dynamic .dynsym .dynstr .hash .gnu.hash)
		*(.comment .note .note.*)
		*(.eh_frame .eh_frame_hdr)
	}
}
