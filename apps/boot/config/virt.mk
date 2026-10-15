ARCH     := arm64
PLATFORM := virt

QEMU       ?= qemu-system-aarch64
QEMU_ARGS  := -machine virt -cpu cortex-a57 -m 1G
QEMU_EXTRA ?=

MODULES := \
	apps/boot \
	arch/$(ARCH) \
	drivers/pci \
	drivers/power/psci \
	drivers/primecell/pl011 \
	drivers/qemu/fwcfg \
	drivers/qemu/ramfb \
	drivers/virtio \
	lib/blockdev \
	lib/cksum \
	lib/crypto \
	lib/devicetree \
	lib/fs \
	lib/heap \
	lib/image \
	lib/libc \
	lib/memmap \
	lib/net \
	lib/partition \
	platform/$(PLATFORM) \
	sys
