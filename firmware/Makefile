# Falcon HP thermostat - application firmware
#
# Targets:
#   make            build build/falcon-app.elf, .bin, .hex
#   make size       show size report
#   make clean      remove build artefacts
#   make host-test  build and run the host test harness
#   make assets     regenerate the NOR-flash image from Mark-icons-*

TOOLCHAIN ?= ../toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi
CC        := $(TOOLCHAIN)/bin/arm-none-eabi-gcc
OBJCOPY   := $(TOOLCHAIN)/bin/arm-none-eabi-objcopy
SIZE      := $(TOOLCHAIN)/bin/arm-none-eabi-size

TARGET    := falcon-app
BUILD     := build

# The shipped application partition, used by the layout smoke test.
ORIGINAL  ?= ../Falcon-HP-144pin-8M-Mark-261001.hex
SMOKE     := ../tools/smoke_test.py

SRC_DIR   := src
INC_DIR   := include

# Sources: startup + hal + drivers + app + ui + net
C_SOURCES := \
  $(wildcard $(SRC_DIR)/startup/*.c) \
  $(wildcard $(SRC_DIR)/hal/*.c) \
  $(wildcard $(SRC_DIR)/drivers/*.c) \
  $(wildcard $(SRC_DIR)/app/*.c) \
  $(wildcard $(SRC_DIR)/ui/*.c) \
  $(wildcard $(SRC_DIR)/net/*.c)

# The board mapping lives in src/ rather than src/hal/.
C_SOURCES += src/board.c

OBJECTS := $(addprefix $(BUILD)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

# The MCU is an AT32F421: ARMv7-M Cortex-M4 with a hardware FPU. The
# shipped image executes ~780 VFP instructions (vmov/vcvt/vstr/vmul/vdiv/
# vcmp), which a Cortex-M3 cannot execute, and it calls functions with the
# AAPCS-VFP convention, so the rebuild is built the same way: hard float
# with a single-precision FPU (fpv4-sp-d16). See docs/hw-map.md.
CPUFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

DEFS := -DF_CPU=72000000UL

CFLAGS := $(CPUFLAGS) $(DEFS) -I$(INC_DIR) -Og -g3 \
          -Wall -Wextra -Wshadow -Wundef -Wdouble-promotion \
          -ffunction-sections -fdata-sections -fno-common \
          -fno-unwind-tables -fno-asynchronous-unwind-tables \
          -std=gnu11 -MMD -MP

LDFLAGS := $(CPUFLAGS) -T linker/stm32f1xx_app.ld -nostartfiles \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map \
           --specs=nano.specs --specs=nosys.specs

LIBS := -lc -lm -lnosys

.PHONY: all size clean host-test assets rebuild smoke-test

all: $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin size smoke-test

$(BUILD)/%.o: %.c Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJECTS) linker/stm32f1xx_app.ld Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD):
	mkdir -p $(BUILD)

size: $(BUILD)/$(TARGET).elf
	@$(SIZE) $<

# Layout regression guard: the rebuilt image must land where the
# bootloader expects it (vector table at 0x08005800, code at 0x08005950,
# identical initial SP, nothing in the shared parameter block).
smoke-test: $(BUILD)/$(TARGET).hex
	@python3 $(SMOKE) --original $(ORIGINAL) --built $<

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/*.d)
