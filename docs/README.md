# Falcon HP thermostat — firmware reverse-engineering and rebuild

This directory documents the analysis of the shipped Energex/Brande **Falcon HP**
thermostat controller firmware and the plan for a clean-room C rebuild.

The firmware under study is an **STM32F1xx-class (Cortex-M3, ARMv7-M Thumb-2)**
application for a 144-pin LQFP device with 1 MB flash and 96 KB SRAM, driving a
colour touch display, an ESP-07S WiFi/BLE module, a LoRaWAN modem, and a
heat-pump HVAC system.

## Documents

| File | Contents |
|---|---|
| [`flash-map.md`](flash-map.md) | Memory map, boot chain, vector table, the shared parameter block |
| [`hw-map.md`](hw-map.md) | Peripheral census, GPIO/relay/terminal map, sensors, panel |
| [`subsystems.md`](subsystems.md) | WiFi/BLE, LoRaWAN, MQTT, BACnet, Modbus, OTA, UI |
| [`hvac-spec.md`](hvac-spec.md) | HVAC state machine, staging, defrost, humidity, schedules |
| [`assets.md`](assets.md) | Icon set, BMP formats, the FAT32 image on external NOR |
| [`toolchain.md`](toolchain.md) | Build system, host test harness, analysis tooling |
| [`open-questions.md`](open-questions.md) | Everything still unresolved, and how to close it |

## Quick orientation

- **What the input is.** `Falcon-HP-144pin-8M-Mark-261001.hex` is *only the
  application partition* of the device flash. A separate bootloader lives at
  `0x08000000`, supplied as `at32_bootloader_6M.hex`. Between them sits a
  runtime-written parameter block that is in neither file.
- **The rebuild is staged, not a port.** The goal is a behaviourally equivalent
  reimplementation in plain C, built from the ground up against a recovered
  specification, not a transliteration of the original binary.
- **Everything here is evidence-backed.** Each claim records how it was
  established. Where something is inferred rather than observed, it is marked
  as such.

## Repository layout

```
Falcon-HP-144pin-8M-Mark-261001.hex   shipped application partition (input)
at32_bootloader_6M.hex                shipped bootloader partition (input)
Mark-icons-2025.10.29/                UI icon set (input)

firmware/                              the rebuild
  include/                             headers
  src/startup/                         reset vector + C runtime
  src/hal/                             clock, GPIO
  src/drivers/                         parameter block
  src/app/                             HVAC state machine, main
  src/ui/                              framebuffer, BMP decoder, FAT32 asset store
  linker/                              linker script (app partition layout)
  tests/                               host test harness

tools/
  hex2elf.py                           wrap a raw image in ELF for REA/Hopper
  mkassets.py                          build the FAT32 NOR image from the icons
  analyze.py                           (planned) capstone-based static analyser

docs/                                  this documentation
TODO.md                                numbered work plan
```

## Current state

- Application firmware **builds** for `cortex-m3` and links to the exact
  partition layout of the original (`0x08005800` vector table, code at
  `0x08005950` — verified identical to the shipped image).
- **43/43 host tests pass**, covering the HVAC state machine, framebuffer,
  BMP decoder and the FAT32 asset store.
- The FAT32 NOR image builds to exactly 2 MiB and mounts; all four icon
  subdirectories and the root directory resolve.
- See [`TODO.md`](../TODO.md) for the numbered plan and
  [`open-questions.md`](open-questions.md) for what remains unknown.
