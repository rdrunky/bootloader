local_dir := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

OBJS += $(local_dir)/ed25519.o
OBJS += $(local_dir)/sha256.o
OBJS += $(local_dir)/sha512.o
