# Open questions

Everything here is unresolved. Each entry states what is known, why it
matters, and how to close it. Items are ordered by impact on the rebuild.

---

## 1. How are the sensors reached? (no I²C or ADC in the peripheral map) — **highest impact**

**Known.** A correct literal-pool census of the shipped code shows **zero**
references to `I2C1` (`0x40005400`), `I2C2` (`0x40005800`), `ADC1`
(`0x40012400`), `ADC2/ADC3` (`0x40012800`) — while the firmware
demonstrably uses an AT24C256 EEPROM, an S35390 RTC, an SI7021 humidity
sensor, three NTC thermistors and a light sensor.

**Why it matters.** This determines the entire sensor HAL. If the buses are
bit-banged on GPIOs, the rebuild needs a bit-bang I²C driver and the exact
pin assignments; if the sensors hang off the ESP-07S or another device, the
host MCU never touches them.

**How to close it.** Trace the self-test functions that produce
`2. EEPROM AT24C256 ok.` (`0x08016048`), `4. RTC S35390 ok.` (`0x080160C3`)
and `5. Humidity SI7021 fail` (`0x080160EB`) back to their callers, then
follow those calls into the register accesses. Watch for:

- GPIO open-drain patterns (CRL/CRH configured with `CNF_OUT_OD` or
  `CNF_IN_FLOAT` plus writes to `BSRR`/`BRR`) → bit-banged I²C
- a write to `0x40010800 + 0x08` (GPIOA IDR) reading a sensor → ADC
  replaced by a comparator/DAC front end
- calls into a UART driver → sensors behind the ESP-07S

---

## 2. Which MCU revision? (Cortex-M3 vs M4, FPU, exact part)

**Known.** The register map is STM32F1xx. The PCB chip is marked
`AT32F421`. The `AT32` string in the firmware is the NOR volume label,
not a chip ID — so the chip identity rests on the photograph alone.
The F103 family tops out at 1 MB flash; the board's `MC6-U-2-L` and the
8 MB figure in the input filename (`…8M…`) are unexplained by an F103.

**Why it matters.** `AT32F421` is a **Cortex-M4** part with an FPU and
different flash/RAM sizes from an F103. The rebuild currently targets
`cortex-m3` with `soft` float ABI. If the real part is M4, the ABI and
the FPU should be used, and RAM/flash sizes change.

**How to close it.** Read the exact chip marking from a higher-resolution
photo (the current one is too soft to read the last two digits
confidently). Confirm against the AT32F421 datasheet: flash size, SRAM
size, and whether its peripherals are F1- or F4-compatible. Note that the
firmware's F1-style map (RCC `0x40021000`, FLASH `0x40022000`, GPIOA
`0x40010800`) is decisive for the *software*; only the core and the
memory sizes are in question.

---

## 3. The payload layout of the shared parameter block at `0x08005000`

**Known.** Magic `0x12345678` at offset 0; payload from offset 4; written
by the bootloader and read/written by the application at six sites
(`0x08012B70`, `0x08063A04`). See [flash-map.md](flash-map.md).

**Why it matters.** It is the bootloader↔application contract. If the
vendor bootloader validates it, a rebuild with a different layout would
be rejected at boot; and settings that the bootloader reads would be lost.

**How to close it.** Either obtain the bootloader source, or read
`0x08005000`–`0x080057FF` from a physical unit and correlate the bytes
with the application's read/write sites by tracing which offsets are
passed to the read/write helpers at `0x08070C9C` / `0x08070DF0`.

---

## 4. The exact stage-2 / balance-point rule

**Known.** Stage 2 exists, is published per-relay, and is gated by a soak
timer. The firmware has a distinct menu entry
`%d. Aux-Heat/HP Balance Temperature` (`0x08014A13`), so stage 2 is not
simply "heat setpoint + N".

**Why it matters.** Determines when a 2-stage heat pump calls for aux heat.

**How to close it.** Decompile the handler reached from the `W2` relay
handlers (`0x080426A8`) and the balance-point configuration code around
`0x08014A13`, and identify the comparison that promotes stage 1 to stage 2.

---

## 5. Modbus register map and BACnet object table

**Known.** A 26-entry point registry exists at `0x08090528`–`0x08090990`
as four-word descriptors `{name, label, value_or_RAM_address, handler}`.
Names are recovered; register addresses and BACnet object identifiers are
not.

**Why it matters.** Without the register map, a Modbus/BACnet client
cannot interoperate with the device.

**How to close it.** Decompile the Modbus TCP server (referenced by
`Modbus Tcp Server Start...`, `0x0806AC23`) and the BACnet server
(`bacnetip servo start...`, `0x0806B098`) and read the register/object
tables they consult.

---

## 6. The MQTT payload format and the AES key usage

**Known.** `www.brande.com` and `mc6mqtt_aes256ecb7` are adjacent in the
string table (`0x0807E66F`); `public1256` also exists (`0x0807E69C`). The
JSON key set is fully recovered.

**Why it matters.** Whether the payload is encrypted, and with which key
and mode, decides if the rebuild can talk to the vendor cloud.

**How to close it.** Find the AES routine (256-key block size, 128 KiB
tables) and the code that references the key string; determine the mode
(ECB suggested by the 16-byte key length and the string's name) and
whether encryption is applied to the whole payload or per field.

---

## 7. The display controller and its SPI command set

**Known.** Panel is 480×320 colour over SPI. Framebuffer init byte
sequences are present (`0x36` MADCTL, `0x3A` PIXDIM, `0x11` SLPOUT,
`0x29` DISPON) but no controller name string exists.

**Why it matters.** The display driver must issue the right controller
sequence and set the correct orientation and colour format.

**How to close it.** Dump the SPI traffic to the panel during
initialisation (logic analyser on the board, or read the SPI TX buffer
from a debugger), then match the sequence against known controllers
(ILI9488, ST7796, NT35510, R61529 …). Alternatively the panel module on
the FPC may have a part number.

---

## 8. The 100 missing UI assets

**Known.** `Gray\` is missing 30 variants, `\Heatbar\` is missing **all
31**, and 39 root icons are missing. See [assets.md](assets.md).

**Why it matters.** The heat-bar UI element and the greyscale scale
variants cannot be reproduced from the supplied material.

**How to close it.** Obtain the 2026-10-01 icon drop, or read the device's
NOR flash to recover the exact bitmaps the original firmware used. The
`Heatbar` set is also the only evidence of a "heat bar" screen, so its
layout must be recovered before that screen can be rebuilt.

---

## 9. Which GPIO drives which relay and sensor

**Known.** The six relay *functions* are known from the terminal
silkscreen and the firmware's terminal-definition strings. The
port/pin *assignments* are not.

**Why it matters.** `firmware/src/board.c` currently carries placeholders
for all six relay outputs.

**How to close it.** Trace the relay drivers: the functions behind
`W1relaystatus` (`0x08042674`), `W2relaystatus` (`0x080426A8`),
`Y1relaystatus` (`0x08013268`), `Y2relaystatus` (`0x0801329C`),
`Fanrelaystatus` (`0x0803EA9C`) will write to a GPIO port; the port
address and pin mask give the mapping directly.

---

## 10. UART assignment between the ESP-07S and the LoRa modem

**Known.** Five UARTs exist in the code; IRQ 37 is USART1 and is used by a
byte-oriented driver; IRQ 52 is UART4. The ESP-07S uses the Espressif AT
set, the LoRa modem the Dragino-style set.

**Why it matters.** The rebuild needs to know which physical UART carries
which module, and the baud rates.

**How to close it.** Decompile the AT-command senders for each dialect and
see which USART structure each touches; the AT string sets are
distinguishable (`AT+BLEINIT` vs `AT+DTRX`).

---

## 11. Scheduler and timing model

**Known.** No RTOS markers were found (no FreeRTOS, uCOS, ThreadX
strings), so the firmware is a bare-metal super-loop with a tick.
`WM_AT_TCP_SEND_TEXT` / `WM_AT_TCP_CLOSE` (`0x0806B0BC`, `0x0806B0F0`)
imply a **window/message** model for the UI and network layers.

**Why it matters.** The rebuild's main loop structure, tick period and
task ordering.

**How to close it.** Find the main super-loop (the function containing
the reset handler's callee), and the tick source. Identify the message
dispatch table behind the `WM_*` names.

---

## 12. Behavioural verification against the original

**Known.** There is no physical unit available, so the rebuild cannot be
compared against real hardware.

**Why it matters.** Every behavioural rule in
[hvac-spec.md](hvac-spec.md) is currently an *inference* from strings and
plausible control flow, not an observation.

**How to close it.** In order of value:

1. **Static differential testing.** Decompile the original's control
   functions and compare the reconstructed C against them line by line.
   This is the highest-value activity and needs no hardware.
2. **Host-side differential harness.** Build a harness that feeds both the
   original's logic (recovered as a C transliteration) and the rebuild
   identical input sequences, comparing relay outputs. Achievable
   entirely in software.
3. **Physical unit**, when one becomes available: read the parameter
   block, log the relays over a controlled temperature sweep, and diff
   against the rebuild's model.

---

## 13. Lesser unknowns

- **Cloud endpoint paths.** Only `/HEader/SerialNumber/RoomDevices/
  RoomTemperature1/225` is known; the rest of the REST surface is not.
- **LoRaWAN payload format.** The AT command set is known; the uplink
  payload structure is not.
- **JSON command grammar.** All keys are known; which are settable,
  their types and validation rules are not.
- **`MC6_E` / `MC6_2FCU` / `MC6_4FCU` variants.** These product-variant
  identifiers imply compile-time variants; which features each enables is
  unknown, and the firmware appears to support several hardware
  configurations from one binary.
- **Timezone/DST rules.** The zone list and `dst.bmp` are known; the
  transition algorithm is not.
- **`0x08001258`–`0x08004FFF` (15,784 bytes)** is in neither input file.
  Whether it is reserved flash or an uncollected partition is unknown.
- **`0x08094238`–`0x080FFFFF`** (~460 KB) is likewise in neither file.
  The 9 code-referenced addresses in that range (up to `0x080EF04F`) are
  unresolvable without it.
