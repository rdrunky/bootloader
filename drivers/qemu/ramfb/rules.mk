local_dir := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

OBJS += \
	$(local_dir)/font.o \
	$(local_dir)/ramfb.o
