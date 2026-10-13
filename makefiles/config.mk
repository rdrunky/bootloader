CROSS   ?= aarch64-linux-gnu-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
READELF := $(CROSS)readelf
NM      := $(CROSS)nm

GCC_INCLUDE := $(shell $(CC) -print-file-name=include)

OPTIMIZATION ?= -Os

INCLUDES := \
	include \
	include/posix \
	arch/$(ARCH)/include \
	platform/$(PLATFORM)/include

WARNINGS := \
	-Wall -Wextra -Werror -Wshadow -Wundef -Wvla \
	-Wmissing-prototypes -Wstrict-prototypes -Wframe-larger-than=2048

CFLAGS := \
	-std=gnu11 $(OPTIMIZATION) -g $(WARNINGS) \
	-ffreestanding -fno-common -fno-strict-aliasing -fno-tree-loop-distribute-patterns \
	-fpie -fvisibility=hidden -ffunction-sections -fdata-sections \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fno-optimize-sibling-calls \
	-fstack-protector-strong \
	-nostdinc -isystem $(GCC_INCLUDE) $(addprefix -I,$(INCLUDES)) \
	-MMD -MP

LDFLAGS := \
	-nostdlib -static-pie -Wl,--no-dynamic-linker -Wl,-z,text -Wl,--gc-sections -Wl,-z,pack-relative-relocs \
	-Wl,--build-id=none -Wl,-z,max-page-size=4096 -Wl,--no-warn-rwx-segments

OBJS :=
