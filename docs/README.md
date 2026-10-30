# Falcon HP thermostat — firmware reverse-engineering and rebuild

This directory documents the analysis of the shipped Energex/Brande **Falcon HP**
thermostat controller firmware and the plan for a clean-room C rebuild.

The firmware under study is an **AT32F421-class (Cortex-M4 with FPU, ARMv7-M
Thumb-2)** application for a 144-pin LQFP device, driving a colour touch
display, an ESP-07S WiFi/BLE module, a LoRaWAN modem, and a heat-pump HVAC
system. The peripheral map is STM32F1-compatible except for the ADC, which
sits at the AT32 address `0x40013C00`.

## Documents

| File | Contents |
|---|---|
| [`flash-map.md`](flash-map.md) | Memory map, boot chain, vector table, the shared parameter block |
| [`hw-map.md`](hw-map.md) | Peripheral census, GPIO/relay/terminal map, sensors, panel |
| [`subsystems.md`](subsystems.md) | WiFi/BLE, LoRaWAN, MQTT, BACnet, Modbus, OTA, UI |
| [`hvac-spec.md`](hvac-spec.md) | HVAC state machine, staging, defrost, humidity, schedules |
| [`assets.md`](assets.md) | Icon set, BMP formats, the FAT32 image on external NOR |
| [`toolchain.md`](toolchain.md) | Build system, host test harness, analysis tooling |
| [`cloud-contract.md`](cloud-contract.md) | The vendor's cloud: MQTT, HTTP, telemetry, OTA |
| [`wire-formats.md`](wire-formats.md) | Byte-level formats: Modbus, MQTT-SN, HTTP, JSON, AT |
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
  analyze.py                           Thumb-2 flow analyser (pools, strings,
                                       call graph, peripheral census)

docs/                                  this documentation
TODO.md                                numbered work plan
```

## Current state

- The MCU is an **AT32F421 (Cortex-M4F)** — established from the VFP
  instruction stream and the AT32 ADC address. The rebuild now compiles
  for `cortex-m4` with hard float.
- Application firmware **builds** and links to the exact partition layout
  of the original (`0x08005800` vector table, code at `0x08005950` —
  verified identical to the shipped image).
- **326/326 host tests pass**, covering the HVAC state machine,
  framebuffer, BMP decoder, the FAT32 asset store, the recovered board
  pin map, the digit rendering, the USB device, the display driver, the
  two BACnet servers, the OTA client, the LoRaWAN payload, the time and
  humidity sources, and the screen and touch layers.
- The **differential harness** compares the HVAC state machine against an
  independent transcription of the control rules over **82,944** cases
  and finds no divergence.
- The FAT32 NOR image builds to exactly 2 MiB and mounts; all four icon
  subdirectories and the root directory resolve.
- See [`TODO.md`](../TODO.md) for the numbered plan and
  [`open-questions.md`](open-questions.md) for what remains unknown.

### What the last pass established

| Question | Answer | Evidence |
|---|---|---|
| What is the USB device? | a **CDC-ACM virtual COM port** | the descriptor set at `0x08084938` |
| Which SPI bus is the panel? | **SPI3** | the byte-write helper at `0x0807E730` |
| Is the RTC on a bus? | **no** — the time arrives over the building bus | the writers of `0x200021C8`+`0x14` |
| Is the humidity sensor on a bus? | **no** — one byte in the state block | the writers of `0x20002248`+`0x291` |
| How many digit families? | **eleven**, not six | the asset inventory |
| What is the point registry? | four-word records, and the full list of published points | `0x08090528` |
