DEMO_DISK  := $(TEST_AUTOBOOT)
PHONE_ARGS := -machine virt -cpu cortex-a72 -smp 4 -m 2G -device virtio-rng-pci \
	      -fw_cfg name=opt/boot/policy,string=locked
shell_quote = '$(subst ','"'"',$(1))'

.PHONY: demo phone

demo: $(BIN) $(DEMO_DISK)
	python3 -B tools/run.py --qemu "$(QEMU)" --kernel "$(BIN)" --disk "$(DEMO_DISK)" \
		--qemu-args=$(call shell_quote,$(QEMU_ARGS) $(QEMU_EXTRA))

phone: $(BIN) $(TEST_PHONE)
	python3 -B tools/run.py --qemu "$(QEMU)" --kernel "$(BIN)" --disk "$(TEST_PHONE)" \
		--qemu-args=$(call shell_quote,$(PHONE_ARGS) $(QEMU_EXTRA)) \
		--drive "-drive file=$(TEST_PHONE),format=raw,if=virtio,snapshot=on"
