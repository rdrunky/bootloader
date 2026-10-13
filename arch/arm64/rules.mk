local_dir := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

CFLAGS   += -march=armv8-a -mgeneral-regs-only -mstrict-align
LDSCRIPT := $(local_dir)/link.ld

OBJS += \
	$(local_dir)/cache.o \
	$(local_dir)/cpu.o \
	$(local_dir)/exceptions.o \
	$(local_dir)/handlers.o \
	$(local_dir)/handoff.o \
	$(local_dir)/mmu.o \
	$(local_dir)/probe.o \
	$(local_dir)/start.o \
	$(local_dir)/stubs.o \
	$(local_dir)/timer.o
