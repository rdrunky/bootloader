local_dir := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

OBJS += \
	$(local_dir)/autoboot.o \
	$(local_dir)/boot.o \
	$(local_dir)/bootcfg.o \
	$(local_dir)/commands/boot.o \
	$(local_dir)/commands/debug.o \
	$(local_dir)/commands/devices.o \
	$(local_dir)/commands/display.o \
	$(local_dir)/commands/files.o \
	$(local_dir)/commands/memory.o \
	$(local_dir)/commands/network.o \
	$(local_dir)/commands/storage.o \
	$(local_dir)/commands/system.o \
	$(local_dir)/convert.o \
	$(local_dir)/diagnostics.o \
	$(local_dir)/key.o \
	$(local_dir)/layout.o \
	$(local_dir)/location.o \
	$(local_dir)/main.o \
	$(local_dir)/recovery.o \
	$(local_dir)/security.o \
	$(local_dir)/slots.o
