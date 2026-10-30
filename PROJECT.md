# Falcon HP thermostat — project structure and reverse-engineering summary

A single self-contained reference for the whole project: what the target
is, how the repository is organised, what has been recovered from the
shipped binaries, what has been rebuilt so far, and what remains.

Companion documents — read these for the detail:

| Document | Covers |
|---|---|
| [`docs/README.md`](docs/README.md) | Orientation and document index |
| [`docs/flash-map.md`](docs/flash-map.md) | Partitions, boot chain, vector table, parameter block |
| [`docs/hw-map.md`](docs/hw-map.md) | Peripheral census, terminals, sensors, radios |
| [`docs/subsystems.md`](docs/subsystems.md) | All protocol surfaces |
| [`docs/hvac-spec.md`](docs/hvac-spec.md) | HVAC control rules |
| [`docs/assets.md`](docs/assets.md) | Icon set, BMP formats, NOR image |
| [`docs/toolchain.md`](docs/toolchain.md) | Build system, test harness, analysis tooling |
| [`docs/open-questions.md`](docs/open-questions.md) | 13 unresolved items |
| [`docs/cloud-contract.md`](docs/cloud-contract.md) | The vendor's cloud contract |
| [`docs/wire-formats.md`](docs/wire-formats.md) | Byte-level protocol formats |
| [`TODO.md`](TODO.md) | Numbered work plan |

---

# 1. What this project is

## 1.1 The subject

The input is the firmware of a **Falcon HP** room thermostat — a
heat-pump HVAC controller made by **Energex / Brande** under the **MC6**
product platform. The unit has:

- a **480 × 320 colour touch display**
- an **ESP-07S** module (ESP8266, WiFi **and** BLE) driven over UART
- a **LoRaWAN modem** on a second UART
- a **2-stage reversible heat-pump** interface (heat, aux heat, two cooling
  stages, fan, reversing valve)
- **I²C** sensors: EEPROM, RTC, humidity
- **NTC** thermistors (on-board, in-door, out-door) and a light sensor
- a **parallel NOR flash** holding the UI bitmaps as a FAT32 filesystem
- **Modbus TCP + Modbus RTU 485 + BACnet IP/MS-TP**, MQTT cloud, and
  FTP over-the-air update

## 1.2 The approach

This is a **clean-room reimplementation**, not a disassembly-to-C port.
The original is a closed ARM Compiler (Keil MDK) build with no symbols.
The work is therefore:

1. **Recover a specification** from the binary — memory map, peripheral map,
   protocol surfaces, control rules — recording the evidence for each claim.
2. **Rebuild** the firmware in plain C from that specification, targeting the
   same MCU and the same flash layout.
3. **Verify** against the original, primarily by static differential testing,
   since no physical unit is available.

The rule applied throughout: **every statement records how it was
established**, and inferences are marked separately from observations.

## 1.3 Two source binaries, one device

| File | Role |
|---|---|
| `Falcon-HP-144pin-8M-Mark-261001.hex` | Application partition only — **not** a whole-device image |
| `at32_bootloader_6M.hex` | The bootloader, at flash base |
| `Mark-icons-2025.10.29/` | The UI icon set (a *different revision* — see §5.4) |

---

# 2. Device architecture as recovered

## 2.1 Flash map

```
0x08000000  ┌──────────────────────┐
            │ BOOTLOADER            │  4,696 B   at32_bootloader_6M.hex
0x08001258  ├──────────────────────┤
            │ reserved / erased     │  15,784 B  in neither file
0x08005000  ├──────────────────────┤
            │ SHARED PARAM BLOCK    │  8 KB      runtime-written, in neither file
0x08005800  ├──────────────────────┤
            │ app VECTOR TABLE      │  336 B  ┐
0x08005950  │                      │          ├ Falcon-HP-…hex
            │ app CODE + DATA       │  583,912 B┘
0x08094238  ├──────────────────────┤
            │ not covered           │  ~460 KB  in neither file
0x08100000  └──────────────────────┘
```

## 2.2 Boot chain

```
reset → bootloader (0x08000000)
          ├─ validates / writes the shared parameter block at 0x08005000
          └─ jumps to the application at 0x08005800
                └─ app sets SCB->VTOR = 0x08005800 and runs
```

Both facts are literal constants in the shipped code: the bootloader holds
`0x08005800` at `0x08000BD0` and `0x08005000` at `0x08000BBC`; the
application writes `SCB->VTOR` at `0x0807AA98` and references
`0x08005000` from six sites.

## 2.3 MCU

**AT32F421-class MCU, ARMv7-M Cortex-M4 (Thumb-2) with hardware FPU,
LQFP144.** The PCB's QFP is marked `AT32F421`. Three independent
observations fix the core:

1. the image executes ~780 VFP instructions (`vmov`, `vcvt`, `vstr`,
   `vmul`, `vdiv`, `vcmp`) — impossible on a Cortex-M3;
2. the ADC is used at **`0x40013C00`**, the AT32 ADC address, not at the
   STM32F1 ADC1 address `0x40012400`;
3. `0x40012404` is referenced separately (once, at `0x08066B18`), so the
   two addresses are different peripherals of the AT32 map.

The rest of the peripheral map is F1-compatible:

| Peripheral | Address | Refs |
|---|---|---|
| RCC | `0x40021000` | 10 |
| FLASH | `0x40022000` | 7 |
| GPIOA … GPIOG | `0x40010800` … `0x40012000` | 28 / 32 / 17 / 4 / 1 / 5 / 11 |
| USART1 / USART3 | `0x40013800` / `0x40004800` | 5 / 12 |
| UART4 / UART5 | `0x40004C00` / `0x40005000` | 5 / 12 |
| SPI1 / SPI3 | `0x40013400` / `0x40003C00` | 2 / 4 |
| USB | `0x40005C40`–`0x40006000` | 28 |

The USB block is a **CDC-ACM virtual COM port**: the descriptor set at
`0x08084938` is a device descriptor with idVendor `0x0483` and idProduct
`0x5720` (STMicroelectronics' virtual COM port pair), one configuration,
one CDC-ACM interface and two bulk endpoints (`0x81` in, `0x02` out,
64 bytes), with the strings "Heatmiser", "Accent" and "ST Mass". The
handler at `0x0800FF74` bridges the CDC data to UART5.
| FSMC | `0xA0000000` | 2 |

Every F4-map alternative was searched for and **not found** (RCC
`0x40023800`: 0 hits; FLASH `0x40023C00`: 0 hits). The initial SP
`0x20016B20` requires ≥ 93 KB of SRAM.

**Consequence:** the rebuild's `-mcpu=cortex-m3 -mfloat-abi=soft` is
known to be wrong; TODO 4.7 switches it to Cortex-M4 with hard float.


## 2.4 Terminals — from the PCB and the firmware agreeing

PCB silkscreen (rear) `B A G W2 W Y2 | Y1 O R Opd Gnd St` matches the
firmware's own *Terminal Definition* strings exactly:

| Terminal | Meaning | Firmware string | GPIO [F] |
|---|---|---|---|
| `W` | heat stage 1 | `Heating--W_OB` | PA3 |
| `W2` | heat stage 2 / aux heat | `Aux Heating--W2_AUX` | PA1 |
| `Y` | cool stage 1 | `1st compressor--Y` | PB13 |
| `Y2` | cool stage 2 | `2nd compressor--Y2` | PA2 |
| `G` | fan | `Fan--G` | PB14 |
| `O/B` | reversing valve | `O/B Valve--W_OB` | `[I]` unknown |

⇒ a **2-stage reversible heat pump**. The five GPIO assignments come
from the point-registry relay handlers (`0x08042674`, `0x080426A8`,
`0x08013268`, `0x0801329C`, `0x0803EA9C`), each of which reads one pin,
and are confirmed by the output init at `0x0807DC5C`.


---

# 3. Repository structure

```
Falcon-HP-144pin-8M-Mark-261001.hex   input: shipped application partition
at32_bootloader_6M.hex                input: shipped bootloader partition
Mark-icons-2025.10.29/                input: UI icon set (419 files)

PROJECT.md                             this file
TODO.md                                numbered work plan
.gitignore

docs/                                  engineering documentation
  README.md  flash-map.md  hw-map.md  subsystems.md
  hvac-spec.md  assets.md  toolchain.md  open-questions.md

firmware/                              the rebuild (C)
  Makefile                             cross-build + size report
  linker/
    stm32f1xx_app.ld                   app-partition linker script
  include/                             headers
    stm32f1xx.h                        bare F1 register definitions
    system.h  gpio.h  board.h  hvac.h
    bmp.h  framebuffer.h  asset_store.h  param_block.h
    digits.h  screen.h  touch.h  panel.h  usb.h
    bacnet.h  ota.h  lora_payload.h  rtc.h  si7021.h
  src/
    startup/startup_stm32f1xx.c        reset vector + C runtime
    hal/system.c                       clock: HSE→PLL×9→72 MHz
    hal/gpio.c                         GPIO access
    hal/uart.c  spi.c  i2c.c  adc.c  timer.c  dma.c  fsmc.c
    hal/sensor_bus.c                   the GPIOC two-wire bus
    hal/usb.c                          the CDC-ACM device
    hal/panel.c                        the MIPI-DCS display driver
    drivers/param_block.c              shared parameter block
    drivers/eeprom.c                   AT24C256 over the bit-banged bus
    drivers/rtc.c  si7021.c            the RAM-backed time and humidity
    app/hvac.c                         HVAC state machine
    app/schedule.c  meter.c  faults.c  sensors.c  selftest.c
    app/scheduler.c  main.c
    ui/framebuffer.c                   RGB565 framebuffer
    ui/bmp.c                           BMP decoder, whole-image and streaming
    ui/asset_store.c  nor_store.c      FAT32 reader over the NOR
    ui/digits.c                        the eleven numeric glyph families
    ui/screen.c  touch.c               the widget and touch layers
    net/at_client.c  esp_at.c  lora_at.c
    net/http_client.c  mqtt_sn.c  modbus.c
    net/bacnet.c                       the BACnet IP and MS/TP servers
    net/ota.c                          the FTP update client
    net/lora_payload.c                 the LoRaWAN uplink codec
    board.c                            board pin map
  tests/
    Makefile                           host build
    test_main.c                        the main harness
    test_protocols.c                   the protocol and UI checks
    differential.c                     the differential harness
    fake_i2c.c  fake_sensor_bus.c      simulated devices

tools/                                 analysis and packaging
  hex2elf.py                           flat image → ELF for REA/Hopper
  analyze.py                           Thumb-2 flow analyser: pools, strings,
                                       call graph, peripheral census, and
                                       the `lit` and `table` queries
  mkassets.py                          icons → FAT32 NOR image
  smoke_test.py                        the layout regression

build/                                generated
  nor-image.bin                        2 MiB FAT32 asset image
  asset-manifest.txt                   per-asset ok/MISSING report

toolchains/                            ARM GNU Toolchain 13.3 (not in git)
```

---

# 4. Module map of the rebuild

Every module records the evidence that justifies it.

## 4.1 `startup/`

`startup_stm32f1xx.c` — 84-word vector table (16 core + 68 IRQ) at
`0x08005800`; `Reset_Handler` copies `.data`, zeroes `.bss`, sets
`SCB->VTOR` and calls `main()`. Mirrors the original's shape: 84 words →
`.text` begins at `0x08005950`, **identical to the shipped image**.

## 4.2 `hal/`

- **`system.c`** — 8 MHz HSE → PLL ×9 → 72 MHz, AHB /1, APB1 /2, APB2 /1.
  The original's RCC usage (99 literal refs) shows the same tree.
- **`gpio.c`** — configure/read/write/toggle/masked-write. GPIOA–GPIOG are
  the most-used peripherals in the firmware, so this is the core HAL.

## 4.3 `drivers/param_block.c`

Flash-resident block at `0x08005000`: magic `0x12345678` at offset 0,
payload from offset 4, rewritten through the FLASH controller (page erase
+ halfword program). The **payload layout is still opaque** — see
[`docs/open-questions.md`](docs/open-questions.md) §3.

## 4.4 `app/hvac.c` — the highest-value module

A pure state machine: reads a state struct, writes relay outputs, no
hardware access — so it is fully host-testable.

```
modes (12)        Off Heat Cool Heat&Cool Auto Fan Ventilate
                   EmergencyHeat Dehumidify Humidify Dry Economizer
equipment (17)    Standard Heat/Cool, Heat Pump, 1/2-stage variants, Dual Fuel,
                   O/B valve variants, …
relays (6)        W1 W2 Y1 Y2 G OB
rules             demand evaluation, stage-2 soak, heat/cool exclusivity,
                   reversing valve, fan, defrost, humidity, restart delays
```

## 4.5 `ui/`

- **`bmp.c`** — 32bpp BGRA with a real opacity channel, 24bpp opaque,
  16bpp packed, bottom-up rows, 4-byte row padding. `bmp_draw_stream()`
  fetches the image **one row at a time**, because the largest glyph
  (`nb0.bmp`) is 82,836 bytes and the device has 96 KiB of SRAM.
- **`framebuffer.c`** — RGB565 fill / rect / clipped blit / alpha blend.
- **`asset_store.c`** — FAT32 over an abstract read callback, so the same
  code runs against the real FSMC NOR and an in-memory image.
  Case-insensitive 8.3 path matching, matching the firmware's
  `\White\105.bmp` convention.
- **`digits.c`** — the eleven numeric glyph families the firmware
  references, their sign and decimal-point glyphs, a signed fixed-point
  number renderer and the wifi bars.
- **`screen.c`** — a screen is a list of picture, label, number and
  button widgets; a button carries its hit rectangle.
- **`touch.c`** — touch handling and the touchpad lock.

## 4.6 `net/`

- **`bacnet.c`** — the object table, the ReadProperty encoding, the
  BACnet/IP server (BVLC + NPDU + APDU) and the MS/TP server.
- **`ota.c`** — the FTP update client: the `U-LW-%d.%02d` version
  naming, the comparison, the check and the download-and-apply.
- **`lora_payload.c`** — the LoRaWAN uplink payload codec.

## 4.7 `hal/`

- **`usb.c`** — the CDC-ACM device stack: the control endpoint, the
  class requests and the bulk rings.
- **`panel.c`** — the MIPI-DCS display driver for the 480×320 panel.

## 4.8 `drivers/`

- **`rtc.c`** — the S35390 model bound to the RAM shadow the firmware
  uses.
- **`si7021.c`** — the SI7021 model bound to the humidity byte the
  firmware uses.

## 4.9 `board.c`

Every board-specific fact in one place so drivers stay portable.
Facts are tagged `[F]` (from the firmware), `[P]` (from the PCB) or
`[I]` (inferred, needs the schematic). The six relay pin assignments are
currently **`[I]` placeholders**.

---

# 5. What was recovered from the binary

## 5.1 Self-test sequence

`Hardware Checking...` (`0x08016013`) then, in order: relay → EEPROM →
NOR flash → RTC → humidity → WiFi module → 3× NTC → light sensor.

## 5.1b How the sensors are reached — the key hardware result

| Sensor | Path | Evidence |
|---|---|---|
| AT24C256 EEPROM | **bit-banged I²C on GPIOG** (SDA = PG6, SCL = PG8) | `0x08042A08`–`0x08042C88`, API `0x08042938`, device byte `0xA0`, reply `"#E"` |
| NTC ×3, light sensor | **on-chip ADC at the AT32 address `0x40013C00`** | HAL `0x08066B2C`–`0x08066D98`, sampling `0x0806850C` |
| external NTC channels | **second two-wire bus on GPIOC** (data = PC8, clock = PC9) | primitives `0x08061958`–`0x08061B70`, user `0x08062504` |
| S35390 RTC, SI7021 | not reached over any bus — time and humidity live in RAM | see [`docs/open-questions.md`](docs/open-questions.md) §1 |

There is **no I²C peripheral in the image at all**; the earlier census
that reported "zero ADC references" was wrong because it searched for the
STM32F1 ADC address only.


## 5.2 Protocol surfaces

| Area | Recovered |
|---|---|
| ESP-07S AT | full command set — WiFi join, BLE init/advertise/GATT, **BLUFI** provisioning, TCP/UDP sockets, module reset/status |
| LoRaWAN | full command set — join, ADR, SF7–SF12/BW125, 3–17 dBm, EUI/keys |
| MQTT | MQTT-SN over the ESP-07S; topic config; the full JSON key set; `www.brande.com` + `mc6mqtt_aes256ecb7` (AES-256 key) |
| HTTP | `application/x-www-form-urlencoded`; `/HEader/SerialNumber/RoomDevices/…` path family |
| Modbus | TCP server and RTU-485 server |
| BACnet | IP and MS/TP servers |
| OTA | FTP to `/ftp/Products/MC6-U-Lora/FalconHP_Mark/`, `U-LW-%d.%02d` versions |

## 5.3 The protocol point registry

A table of four-word descriptors at `0x08090528`–`0x08090990`:

```
{ name_ptr, label_ptr, value_or_RAM_address, handler_ptr }
```

26 descriptors recovered, e.g. `W1relaystatus | Heating relay W1 status |
fn 0x08042674`. The third word is sometimes a literal and sometimes a
**live RAM address** (`TempFormat` → `0x20002188`), which is how the
protocol layers reach the HVAC core.

## 5.4 UI assets — and a revision mismatch

419 supplied icons (416 BMP + 3 GIF), 3.5 MB. The firmware references
**486** distinct assets: 191 path-style (`\White\`, `\Gray\`, `\fan\`,
`\nfan\`, `\Heatbar\`) and 295 root-level.

**100 of the 486 are missing** from the supplied set — including the whole
`\Heatbar\` directory (31) and 30 `Gray\` scale variants. 33 supplied
files are unreferenced by this build (other MC6 variants).

**Storage format.** The NOR is 16 Mbit = 2 MiB. The set is 3.5 MiB as
32bpp — it does not fit. As **RGB565 it is 1.75 MiB** and does, which
matches the panel (a 480×320 RGB565 framebuffer is 300 KiB, far beyond the
96 KiB SRAM, so the panel carries its own GRAM). The packer therefore
converts to 16bpp.

The image also uses **1 sector per cluster**: at FAT32's usual cluster
size the per-file rounding waste alone overflows the device.

## 5.5 The original toolchain

The startup code is an **ARM Compiler (Keil MDK / armcc)** runtime:
the `__main` veneer at `0x08005950` and the reset handler's
`ldr/blx` → `ldr/bx` sequence at `0x08005B1C`. The rebuild uses GCC, but
produces the **same memory layout** — vector table at `0x08005800`, code
at `0x08005950` — which is what the bootloader contract depends on.

---

# 6. What has been built

| Artefact | State |
|---|---|
| ARM toolchain 13.3 | installed in-tree, no root needed |
| ELF wrapper for REA/Hopper | `tools/hex2elf.py`; REA reports **685 procedures** |
| Static analyser | `tools/analyze.py` — pools, strings, call graph, census |
| Linker script | reproduces the original partition layout exactly |
| Register header | bare SPL-style F1 definitions, only peripherals in use |
| Startup + vector table | 84 entries; layout **identical** to the original |
| Clock + GPIO HAL | working |
| Bit-banged I2C master | PG6/PG8, host-tested against a simulated AT24C256 |
| AT24C256 EEPROM driver | 32 KiB, `"#E"` signature check, host-tested |
| ADC driver | AT32 ADC at `0x40013C00`, channel select + inverted counts |
| NTC scaling | recovered divider maths; rebuild's own temperature curve |
| Measurement bus | PC8/PC9 two-wire bus, host-tested against a simulated device |
| Boot self-test | relay + EEPROM steps, as the original's sequence |
| Parameter-block envelope | magic + payload, FLASH read/write |
| HVAC state machine | complete; fully host-tested |
| Framebuffer / BMP / FAT32 store | complete; host-tested |
| Asset packer | 2 MiB FAT32 image; missing-asset report + manifest |
| USB device (CDC-ACM) | complete; the descriptor set is the original's |
| Display driver | complete; the panel bus is SPI3 |
| BACnet IP + MS/TP servers | complete |
| OTA update client | complete |
| LoRaWAN payload codec | complete (the field layout is a rebuild choice) |
| Time and humidity sources | complete (both arrive over the building bus) |
| Screen, widget and touch layers | complete |
| Host test harness | **326 checks, 0 failures** |
| Differential harness | **82,944 cases, no divergence** |


Verified layout match:

```
readelf -S build/falcon-app.elf
  [1] .isr_vector  PROGBITS  08005800 000800 000150 00   A
  [2] .text        PROGBITS  08005950 000950 000414 00  AX
```

The initial-SP word `0x20016B20` and the 336-byte table size are
**byte-identical** to the shipped image. Handler addresses differ, as they
must — the rebuild's code is laid out differently.

---

# 7. Method

## 7.1 Analysis

REA 6.7.0 with Hopper 6.6.0 analyses the image through the ELF wrapper.
Ghidra is absent (no `java`), so REA's raw-binary path is unavailable —
the wrapper is the only route.

**REA's string discovery does not work on this target**: `list_strings`
and `search_strings` return nothing, because Hopper does not create string
objects for constants embedded in the executable section — and in this
firmware the strings *are* embedded in code. Hopper also found only 685
procedures and none in the self-test region.

Because of that, `tools/analyze.py` does the heavy lifting: it performs
its own Thumb-2 flow analysis (1,318 functions, 5,324 strings in ~1.5 s)
and recovers literal pools, PC-relative string references, the call graph,
per-function RAM/peripheral usage and the peripheral census. REA remains
useful for an independent look at a function's pseudocode.


## 7.2 Two analysis traps hit, and how they were resolved

1. **Wrong peripheral bucketing.** Bucketing constants by
   `value & 0xFFFFF000` mis-assigns every F1 peripheral, because F1 bases
   carry non-zero low 12 bits (`GPIOA 0x40010800` → bucket `0x40010000`
   = AFIO). An early census was wrong for exactly this reason and had to
   be redone with nearest-base attribution.
2. **SIGPIPE-truncated output.** Piping the asset packer into `head`
   killed it mid-write and left a truncated image, which then read as a
   corrupt filesystem and sent the debugging in the wrong direction. The
   packer must be redirected to a file, never piped.

## 7.3 Verification

- **Host tests** — the hardware-independent logic is compiled with the host
  compiler and asserted. This is the only regression net, so it stays
  green.
- **Layout regression** — the built vector table is compared against the
  original on every build.
- **Differential testing (done, in software)** — `firmware/tests/differential.c`
  compares the HVAC state machine against an independent transcription of
  the control rules over 82,944 cases and finds no divergence. It found
  two real defects on the way: the `1 Cool Stage` equipment type could
  never call for cooling, and the fan in the Fan/Ventilate modes was
  gated by the occupancy input, which the specification does not say.
  The remaining half — transliterating the original's own control
  function (`0x0807F9BC`) — is still open.

---

# 8. Open items

Full detail in [`docs/open-questions.md`](docs/open-questions.md). The
ones that gate the most work:

1. **Parameter-block payload layout** — the bootloader↔application
   contract.
2. **Stage-2 / balance-point rule** — stage 2 is not a simple second
   setpoint.
3. **Modbus register numbers and BACnet object identifiers** — the
   registry's names and handlers are recovered; the numbering is not.
4. **MQTT payload encryption** — mode and key usage.
5. **Display controller** — the bus is SPI3 and the command set is the
   standard MIPI-DCS subset, but the part number is not recoverable
   statically.
6. **The 100 missing assets** — needs the 2026-10-01 icon drop or a NOR
   dump.
7. **The O/B valve pin** — the only relay output not identified.
8. **The transliteration of the original's control function** — the
   remaining half of the differential test.

Closed since the first pass: the MCU revision, the USB device's role
(a CDC-ACM virtual COM port), the panel bus (SPI3), and the RTC and
humidity read path (both arrive over the building bus, not locally).

No physical unit is available, so all verification is currently
software-only.


---

# 9. Working on this

```bash
# build the firmware
cd firmware && make && make size

# run the host tests
cd firmware/tests && make host-test

# rebuild the NOR asset image
python3 tools/mkassets.py --icons Mark-icons-2025.10.29 \
    --firmware Falcon-HP-144pin-8M-Mark-261001.hex \
    --out build/nor-image.bin --manifest build/asset-manifest.txt

# prepare the image for REA/Hopper
tools/hex2elf.py Falcon-HP-144pin-8M-Mark-261001.hex \
    --base 0x08005800 --entry 0x08005950 -o /tmp/app.elf
npx rea-agents@6.7.0 analyze /tmp/app.elf

# or analyse it directly (no Hopper needed, no MCP needed)
/tmp/opencode/.venv/bin/python tools/analyze.py \
    --hex Falcon-HP-144pin-8M-Mark-261001.hex census
```

Work items are tracked in [`TODO.md`](TODO.md); the suggested execution
order starts with the sensor-access trace, because it unblocks the most.
