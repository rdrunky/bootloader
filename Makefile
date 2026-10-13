TARGET ?= virt
BUILD  ?= build

include apps/boot/config/$(TARGET).mk
include makefiles/config.mk

include $(addsuffix /rules.mk,$(MODULES))

include makefiles/build.mk
include makefiles/keys.mk
include makefiles/test.mk
include makefiles/demo.mk
