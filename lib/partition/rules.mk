local_dir := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

OBJS += \
	$(local_dir)/gpt.o \
	$(local_dir)/mbr.o \
	$(local_dir)/partition.o
