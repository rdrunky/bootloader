local_dir := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

OBJS += \
	$(local_dir)/ctype/ctype.o \
	$(local_dir)/stdio/format.o \
	$(local_dir)/stdio/printf.o \
	$(local_dir)/stdio/snprintf.o \
	$(local_dir)/stdio/stdio.o \
	$(local_dir)/stdlib/malloc.o \
	$(local_dir)/stdlib/qsort.o \
	$(local_dir)/string/compare.o \
	$(local_dir)/string/copy.o \
	$(local_dir)/string/error.o \
	$(local_dir)/string/length.o \
	$(local_dir)/string/memory.o \
	$(local_dir)/string/search.o
