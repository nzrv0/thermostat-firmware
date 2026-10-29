# Host test harness
#
# Builds the hardware-independent parts of the firmware (HVAC state
# machine, framebuffer, BMP decoder, FAT32 asset store) with the host
# compiler and runs the checks.

HOSTCC      ?= cc
BUILD       := build-host
FWD         := ../

# Only the portable sources: no hardware register access.
HOST_SOURCES := \
  $(FWD)src/app/hvac.c \
  $(FWD)src/ui/framebuffer.c \
  $(FWD)src/ui/bmp.c \
  $(FWD)src/ui/asset_store.c \
  test_main.c

CFLAGS := -I$(FWD)include -O1 -g -Wall -Wextra -std=gnu11

NOR_IMAGE ?= $(FWD)../build/nor-image.bin

.PHONY: host-test

host-test: $(BUILD)/test_main
	@$(BUILD)/test_main $(NOR_IMAGE)

$(BUILD)/test_main: $(HOST_SOURCES) | $(BUILD)
	$(HOSTCC) $(CFLAGS) $(HOST_SOURCES) -o $@

$(BUILD):
	mkdir -p $(BUILD)
