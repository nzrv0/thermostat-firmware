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
  $(FWD)src/ui/digits.c \
  $(FWD)src/board.c \
  $(FWD)src/hal/gpio.c \
  $(FWD)src/hal/i2c.c \
  $(FWD)src/hal/sensor_bus.c \
  $(FWD)src/drivers/eeprom.c \
  $(FWD)src/app/selftest.c \
  $(FWD)src/app/sensors.c \
  $(FWD)src/app/schedule.c \
  $(FWD)src/app/meter.c \
  $(FWD)src/app/faults.c \
  $(FWD)src/app/scheduler.c \
  $(FWD)src/net/at_client.c \
  $(FWD)src/net/esp_at.c \
  $(FWD)src/net/lora_at.c \
  $(FWD)src/net/http_client.c \
  $(FWD)src/net/mqtt_sn.c \
  $(FWD)src/net/modbus.c \
  $(FWD)src/net/bacnet.c \
  $(FWD)src/net/ota.c \
  $(FWD)src/net/lora_payload.c \
  $(FWD)src/hal/usb.c \
  $(FWD)src/hal/panel.c \
  $(FWD)src/drivers/rtc.c \
  $(FWD)src/drivers/si7021.c \
  $(FWD)src/ui/screen.c \
  $(FWD)src/ui/touch.c \
  fake_i2c.c \
  fake_sensor_bus.c \
  differential.c \
  test_main.c \
  test_protocols.c

CFLAGS := -I$(FWD)include -O1 -g -Wall -Wextra -std=gnu11
LDLIBS := -lm

NOR_IMAGE ?= $(FWD)../build/nor-image.bin

.PHONY: host-test

host-test: $(BUILD)/test_main
	@$(BUILD)/test_main $(NOR_IMAGE)

$(BUILD)/test_main: $(HOST_SOURCES) | $(BUILD)
	$(HOSTCC) $(CFLAGS) $(HOST_SOURCES) -o $@ $(LDLIBS)

$(BUILD):
	mkdir -p $(BUILD)
