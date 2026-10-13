local_dir := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

OBJS += \
	$(local_dir)/ext/dir.o \
	$(local_dir)/ext/ext.o \
	$(local_dir)/ext/map.o \
	$(local_dir)/fat/dir.o \
	$(local_dir)/fat/fat.o \
	$(local_dir)/fs.o
