local_dir := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

OBJS += $(local_dir)/blockdev.o
