local_dir := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

OBJS += \
	$(local_dir)/address.o \
	$(local_dir)/property.o \
	$(local_dir)/tree.o \
	$(local_dir)/write.o
