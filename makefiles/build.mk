ELF        := $(BUILD)/boot.elf
BIN        := $(BUILD)/boot.bin
FIRST_ELF  := $(BUILD)/symbols/first.elf
EMPTY_SYMS := $(BUILD)/symbols/empty.S
TABLE_SYMS := $(BUILD)/symbols/table.S
ALL_OBJS   := $(addprefix $(BUILD)/,$(OBJS))
CONFIGS    := makefiles/config.mk apps/boot/config/$(TARGET).mk

Q := @
ifeq ($(V),1)
Q :=
endif

.PHONY: all run debug dump clean

all: $(BIN)

$(EMPTY_SYMS): tools/symbols.py
	@mkdir -p $(dir $@)
	$(Q)python3 tools/symbols.py $@

$(FIRST_ELF): $(ALL_OBJS) $(EMPTY_SYMS) $(LDSCRIPT)
	@echo "  LD      $@"
	$(Q)$(CC) $(LDFLAGS) -T $(LDSCRIPT) $(ALL_OBJS) $(EMPTY_SYMS) -o $@

$(TABLE_SYMS): $(FIRST_ELF) tools/symbols.py
	@echo "  SYMBOLS $@"
	$(Q)python3 tools/symbols.py --nm $(NM) --elf $< $@

$(ELF): $(ALL_OBJS) $(TABLE_SYMS) $(LDSCRIPT)
	@echo "  LD      $@"
	$(Q)$(CC) $(LDFLAGS) -T $(LDSCRIPT) $(ALL_OBJS) $(TABLE_SYMS) -o $@
	$(Q)python3 tools/symbols.py --check --nm $(NM) --elf $@ $(TABLE_SYMS)
	$(Q)$(READELF) -rW $@ | awk '/^[0-9a-f]+ / { print "$@: unpacked relocation " $$3; bad = 1 } END { exit bad }'
	$(Q)$(READELF) -SW $@ | awk '/ \.data / { print "$@: initialized writable data"; bad = 1 } END { exit bad }'

$(BIN): $(ELF)
	@echo "  BIN     $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(BUILD)/%.o: %.c $(CONFIGS)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S $(CONFIGS)
	@mkdir -p $(dir $@)
	@echo "  AS      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

run: demo

debug: $(BIN)
	$(QEMU) $(QEMU_ARGS) $(QEMU_EXTRA) -nographic -kernel $(BIN) -s -S

dump: $(ELF)
	$(OBJDUMP) -d $<

clean:
	rm -rf $(BUILD)

-include $(ALL_OBJS:.o=.d)
