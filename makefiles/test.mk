HOST_CC             ?= cc
TEST_BUILD          := $(BUILD)/tests
TEST_UNIT           := $(TEST_BUILD)/unit
TEST_DTB            := $(TEST_BUILD)/virt.dtb
TEST_GPT            := $(TEST_BUILD)/gpt.img
TEST_MBR            := $(TEST_BUILD)/mbr.img
TEST_DAMAGED        := $(TEST_BUILD)/damaged.img
TEST_TREE           := $(TEST_BUILD)/tree
TEST_EXT4           := $(TEST_BUILD)/ext4.img
TEST_EXT4_SMALL     := $(TEST_BUILD)/ext4-small.img
TEST_EXT2           := $(TEST_BUILD)/ext2.img
TEST_FAT12          := $(TEST_BUILD)/fat12.img
TEST_FAT16          := $(TEST_BUILD)/fat16.img
TEST_FAT32          := $(TEST_BUILD)/fat32.img
TEST_BOOT_FAT       := $(TEST_BUILD)/bootfs.img
TEST_SYSTEM         := $(TEST_BUILD)/system.img
TEST_AUTOBOOT       := $(TEST_BUILD)/autoboot.img
TEST_AUTOBOOT_FAT   := $(TEST_BUILD)/autoboot-bootfs.img
TEST_AUTOBOOT_FILES := $(TEST_BUILD)/autoboot-files
TEST_DETECT         := $(TEST_BUILD)/detect/kernel
TEST_PHONE          := $(TEST_BUILD)/phone.img
TEST_PHONE_ROOT     := $(TEST_BUILD)/phone
TEST_UNSIGNED       := $(TEST_BUILD)/unsigned/boot/boot.cfg
TEST_TAMPERED       := $(TEST_BUILD)/tampered/boot/boot.cfg
TEST_DATA           := $(TEST_DTB) $(TEST_GPT) $(TEST_MBR) $(TEST_DAMAGED) $(TEST_EXT4) $(TEST_EXT4_SMALL) \
		       $(TEST_EXT2) $(TEST_FAT12) $(TEST_FAT16) $(TEST_FAT32) $(TEST_SYSTEM) $(TEST_AUTOBOOT) \
		       $(TEST_DETECT) $(TEST_PHONE) $(TEST_UNSIGNED) $(TEST_TAMPERED)

BOOT_PARTITION_SECTOR   := 2048
SYSTEM_PARTITION_OFFSET := 42991616

PAYLOAD_DIR := $(TEST_BUILD)/payload
PAYLOAD     := $(PAYLOAD_DIR)/payload.bin

PAYLOAD_SOURCES := \
	tests/payload/start.S \
	tests/payload/main.c \
	lib/cksum/crc32.c \
	lib/devicetree/address.c \
	lib/devicetree/property.c \
	lib/devicetree/tree.c \
	lib/libc/ctype/ctype.c \
	lib/libc/stdio/format.c \
	lib/libc/stdio/snprintf.c \
	lib/libc/string/compare.c \
	lib/libc/string/length.c \
	lib/libc/string/memory.c \
	lib/libc/string/search.c

PAYLOAD_CFLAGS := $(filter-out -fstack-protector-strong -MMD -MP,$(CFLAGS)) -fno-stack-protector

HOST_ASAN := $(shell echo 'int main(void) { return 0; }' | \
	$(HOST_CC) -fsanitize=address -x c - -o /dev/null 2> /dev/null && echo yes)

ifeq ($(HOST_ASAN),yes)
SANITIZERS := -fsanitize=address,undefined -fno-sanitize-recover=all
else
SANITIZERS := -fsanitize=undefined -fsanitize-trap=all
endif

TEST_CFLAGS := \
	-std=gnu11 -O1 -g -Wall -Wextra -Werror -Wshadow \
	$(SANITIZERS) -fno-omit-frame-pointer \
	-ffunction-sections -fdata-sections -Wl,--gc-sections \
	-I include -I tests/unit/harness \
	-DTEST_DTB=\"$(TEST_DTB)\" -DTEST_GPT=\"$(TEST_GPT)\" -DTEST_MBR=\"$(TEST_MBR)\" \
	-DTEST_TREE=\"$(TEST_TREE)\" -DTEST_EXT4=\"$(TEST_EXT4)\" -DTEST_EXT4_SMALL=\"$(TEST_EXT4_SMALL)\" \
	-DTEST_EXT2=\"$(TEST_EXT2)\" -DTEST_FAT12=\"$(TEST_FAT12)\" -DTEST_FAT16=\"$(TEST_FAT16)\" \
	-DTEST_FAT32=\"$(TEST_FAT32)\"

TEST_SOURCES := \
	tests/unit/harness/harness.c \
	tests/unit/apps/bootcfg.c \
	tests/unit/drivers/input.c \
	tests/unit/drivers/ramfb.c \
	tests/unit/lib/crypto.c \
	tests/unit/lib/devicetree.c \
	tests/unit/lib/fs.c \
	tests/unit/lib/heap.c \
	tests/unit/lib/memmap.c \
	tests/unit/lib/net.c \
	tests/unit/lib/storage.c \
	apps/boot/bootcfg.c \
	apps/boot/convert.c \
	drivers/qemu/ramfb/font.c \
	lib/blockdev/blockdev.c \
	lib/cksum/crc32.c \
	lib/crypto/ed25519.c \
	lib/crypto/sha256.c \
	lib/crypto/sha512.c \
	lib/devicetree/address.c \
	lib/devicetree/property.c \
	lib/devicetree/tree.c \
	lib/devicetree/write.c \
	lib/fs/ext/dir.c \
	lib/fs/ext/ext.c \
	lib/fs/ext/map.c \
	lib/fs/fat/dir.c \
	lib/fs/fat/fat.c \
	lib/fs/fs.c \
	lib/heap/heap.c \
	lib/memmap/memmap.c \
	lib/net/arp.c \
	lib/net/dhcp.c \
	lib/net/ethernet.c \
	lib/net/icmp.c \
	lib/net/ipv4.c \
	lib/net/net.c \
	lib/net/udp.c \
	lib/partition/gpt.c \
	lib/partition/mbr.c \
	lib/partition/partition.c

TEST_HEADERS := $(wildcard include/*.h include/*/*.h lib/*/*.h lib/*/*/*.h apps/boot/*.h tests/unit/harness/*.h)
TEST_DRIVERS := \
	drivers/qemu/ramfb/font.h \
	drivers/qemu/ramfb/ramfb.c \
	drivers/virtio/input.c \
	drivers/virtio/private.h \
	sys/console.c

.PHONY: test smoke

test: $(TEST_UNIT) $(TEST_DATA)
	$(TEST_UNIT) $(FILTER)

smoke: $(BIN) $(TEST_DATA)
	python3 -B tests/smoke/run.py --image $(BIN) --qemu $(QEMU) --data $(TEST_BUILD) $(FILTER)

$(TEST_UNIT): $(TEST_SOURCES) $(TEST_HEADERS) $(TEST_DRIVERS)
	@mkdir -p $(dir $@)
	@echo "  HOSTCC  $@"
	$(Q)$(HOST_CC) $(TEST_CFLAGS) $(TEST_SOURCES) -o $@

$(TEST_DTB):
	@mkdir -p $(dir $@)
	@echo "  DTB     $@"
	$(Q)$(QEMU) $(QEMU_ARGS) -machine dumpdtb=$@ -display none > /dev/null

$(TEST_BUILD)/%.img: tests/data/%.sfdisk
	@mkdir -p $(dir $@)
	@echo "  DISK    $@"
	$(Q)rm -f $@ && truncate -s 64M $@ && sfdisk --quiet $@ < $< > /dev/null

$(TEST_BUILD)/fat%.img: $(TEST_TREE)/hello.txt tools/mkfat.py
	@echo "  FAT     $@"
	$(Q)python3 tools/mkfat.py --kind fat$* --size $(if $(filter 12,$*),15,64) --label TESTVOL \
		$(if $(filter 32,$*),--fragment) $(TEST_TREE) $@

$(TEST_BOOT_FAT): $(TEST_TREE)/hello.txt tools/mkfat.py
	@echo "  FAT     $@"
	$(Q)python3 tools/mkfat.py --kind fat32 --size 40 --label BOOT $(TEST_TREE)/boot $@

define system_disk
	@mkdir -p $(dir $@)
	@echo "  DISK    $@"
	$(Q)rm -f $@ && truncate -s 96M $@ && sfdisk --quiet $@ < tests/data/system.sfdisk > /dev/null
	$(Q)dd if=$(1) of=$@ bs=512 seek=$(BOOT_PARTITION_SECTOR) conv=notrunc status=none
	$(Q)mkfs.ext4 -q -F -E offset=$(SYSTEM_PARTITION_OFFSET) -b 4096 -L system -d $(TEST_TREE) $@ 48M
endef

$(TEST_SYSTEM): tests/data/system.sfdisk $(TEST_TREE)/hello.txt $(TEST_BOOT_FAT)
	$(call system_disk,$(TEST_BOOT_FAT))

$(TEST_AUTOBOOT_FAT): $(TEST_TREE)/hello.txt tests/data/boot.cfg tools/mkfat.py tools/digest.py tools/sign.py \
		      $(SIGNING_KEY)
	@echo "  FAT     $@"
	$(Q)rm -rf $(TEST_AUTOBOOT_FILES) && cp -r $(TEST_TREE)/boot $(TEST_AUTOBOOT_FILES)
	$(Q)python3 tools/digest.py tests/data/boot.cfg $(TEST_AUTOBOOT_FILES) $(TEST_AUTOBOOT_FILES)/boot.cfg
	$(Q)python3 tools/sign.py sign $(SIGNING_KEY) $(TEST_AUTOBOOT_FILES)/boot.cfg
	$(Q)python3 tools/mkfat.py --kind fat32 --size 40 --label BOOT $(TEST_AUTOBOOT_FILES) $@

$(TEST_AUTOBOOT): tests/data/system.sfdisk $(TEST_TREE)/hello.txt $(TEST_AUTOBOOT_FAT)
	$(call system_disk,$(TEST_AUTOBOOT_FAT))

$(TEST_DETECT): $(PAYLOAD)
	@mkdir -p $(dir $@)
	$(Q)cp $< $@

$(TEST_PHONE): tests/data/phone.layout tests/data/phone.cfg tests/data/recovery.cfg tools/mkdisk.py tools/digest.py \
	       tools/sign.py $(SIGNING_KEY) $(PAYLOAD) $(TEST_TREE)/hello.txt
	@echo "  PHONE   $@"
	$(Q)rm -rf $(TEST_PHONE_ROOT)
	$(Q)mkdir -p $(TEST_PHONE_ROOT)/system/boot $(TEST_PHONE_ROOT)/recovery/boot $(TEST_PHONE_ROOT)/data
	$(Q)cp $(PAYLOAD) $(TEST_PHONE_ROOT)/system/boot/kernel
	$(Q)cp $(TEST_TREE)/boot/ramdisk $(TEST_PHONE_ROOT)/system/boot/ramdisk
	$(Q)python3 tools/digest.py tests/data/phone.cfg $(TEST_PHONE_ROOT)/system/boot $(TEST_PHONE_ROOT)/system/boot/boot.cfg
	$(Q)cp $(PAYLOAD) $(TEST_PHONE_ROOT)/recovery/boot/kernel
	$(Q)python3 tools/digest.py tests/data/recovery.cfg $(TEST_PHONE_ROOT)/recovery/boot \
		$(TEST_PHONE_ROOT)/recovery/boot/boot.cfg
	$(Q)python3 tools/sign.py sign $(SIGNING_KEY) $(TEST_PHONE_ROOT)/system/boot/boot.cfg \
		$(TEST_PHONE_ROOT)/recovery/boot/boot.cfg
	$(Q)cp $(TEST_TREE)/hello.txt $(TEST_PHONE_ROOT)/data/hello.txt
	$(Q)python3 tools/mkdisk.py tests/data/phone.layout $(TEST_PHONE_ROOT) $@

$(TEST_UNSIGNED): tests/data/phone.cfg tools/digest.py $(PAYLOAD) $(TEST_TREE)/hello.txt
	@echo "  FOLDER  $(dir $@)"
	$(Q)rm -rf $(dir $@) && mkdir -p $(dir $@)
	$(Q)cp $(PAYLOAD) $(dir $@)kernel
	$(Q)cp $(TEST_TREE)/boot/ramdisk $(dir $@)ramdisk
	$(Q)python3 tools/digest.py tests/data/phone.cfg $(dir $@) $@

$(TEST_TAMPERED): $(TEST_UNSIGNED) tools/sign.py $(SIGNING_KEY)
	@echo "  FOLDER  $(dir $@)"
	$(Q)rm -rf $(dir $@) && mkdir -p $(dir $@)
	$(Q)cp $(dir $<)kernel $(dir $<)ramdisk $< $(dir $@)
	$(Q)python3 tools/sign.py sign $(SIGNING_KEY) $@
	$(Q)printf '\tmode recovery\n' >> $@

$(TEST_DAMAGED): $(TEST_GPT)
	@echo "  DISK    $@"
	$(Q)cp $< $@ && printf '\377' | dd of=$@ bs=1 seek=542 conv=notrunc status=none

$(PAYLOAD): $(PAYLOAD_SOURCES) tests/payload/link.ld
	@mkdir -p $(PAYLOAD_DIR)
	@echo "  PAYLOAD $@"
	$(Q)$(CC) $(PAYLOAD_CFLAGS) $(LDFLAGS) -T tests/payload/link.ld $(PAYLOAD_SOURCES) -o $(PAYLOAD_DIR)/payload.elf
	$(Q)$(OBJCOPY) -O binary $(PAYLOAD_DIR)/payload.elf $@

$(TEST_TREE)/hello.txt: tools/mktree.py $(PAYLOAD)
	@echo "  TREE    $(TEST_TREE)"
	$(Q)python3 tools/mktree.py --payload $(PAYLOAD) $(TEST_TREE)

$(TEST_EXT4): $(TEST_TREE)/hello.txt
	@echo "  EXT4    $@"
	$(Q)rm -f $@ && mkfs.ext4 -q -F -b 4096 -L system -d $(TEST_TREE) $@ 32M

$(TEST_EXT4_SMALL): $(TEST_TREE)/hello.txt
	@echo "  EXT4    $@"
	$(Q)rm -f $@ && mkfs.ext4 -q -F -b 1024 -O meta_bg,^resize_inode -L small -d $(TEST_TREE) $@ 32M

$(TEST_EXT2): $(TEST_TREE)/hello.txt
	@echo "  EXT2    $@"
	$(Q)rm -f $@ && mkfs.ext4 -q -F -t ext2 -b 1024 -L legacy -d $(TEST_TREE) $@ 32M
