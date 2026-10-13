DEMO_DISK  := $(TEST_AUTOBOOT)
PHONE_ARGS := -machine virt -cpu cortex-a72 -smp 4 -m 2G -device virtio-rng-pci \
	      -fw_cfg name=opt/boot/policy,string=locked

.PHONY: demo phone

demo: $(BIN) $(DEMO_DISK)
	$(QEMU) $(QEMU_ARGS) $(QEMU_EXTRA) -nographic -kernel $(BIN) \
		-drive file=$(DEMO_DISK),format=raw,if=none,id=disk,snapshot=on \
		-device virtio-blk-device,drive=disk

phone: $(BIN) $(TEST_PHONE)
	$(QEMU) $(PHONE_ARGS) $(QEMU_EXTRA) -nographic -kernel $(BIN) \
		-drive file=$(TEST_PHONE),format=raw,if=virtio,snapshot=on
