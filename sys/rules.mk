local_dir := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

OBJS += \
	$(local_dir)/console.o \
	$(local_dir)/debug.o \
	$(local_dir)/devicetree.o \
	$(local_dir)/driver.o \
	$(local_dir)/format.o \
	$(local_dir)/fuse.o \
	$(local_dir)/halt.o \
	$(local_dir)/init.o \
	$(local_dir)/mem.o \
	$(local_dir)/power.o \
	$(local_dir)/random.o \
	$(local_dir)/readline.o \
	$(local_dir)/shell.o \
	$(local_dir)/storage.o \
	$(local_dir)/symbols.o \
	$(local_dir)/time.o
