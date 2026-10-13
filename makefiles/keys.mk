KEYS            := $(BUILD)/keys
DEVELOPMENT_KEY := $(KEYS)/development.pem
SIGNING_KEY     ?= $(DEVELOPMENT_KEY)
PUBLIC_KEY      := $(KEYS)/public.key
KEY_OBJECT      := $(BUILD)/apps/boot/key.o

$(DEVELOPMENT_KEY):
	@mkdir -p $(dir $@)
	@echo "  KEYGEN  $@"
	$(Q)python3 tools/sign.py keygen $@

$(PUBLIC_KEY): $(SIGNING_KEY) tools/sign.py
	@mkdir -p $(dir $@)
	@echo "  PUBKEY  $@"
	$(Q)python3 tools/sign.py public $< $@

$(KEY_OBJECT): $(PUBLIC_KEY)
$(KEY_OBJECT): CFLAGS += -DPUBLIC_KEY=\"$(PUBLIC_KEY)\"
