local_dir := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

OBJS += \
	$(local_dir)/block.o \
	$(local_dir)/mmio.o \
	$(local_dir)/net.o \
	$(local_dir)/pci.o \
	$(local_dir)/queue.o \
	$(local_dir)/random.o \
	$(local_dir)/virtio.o
