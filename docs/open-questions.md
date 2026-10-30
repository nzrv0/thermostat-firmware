# Open questions

Everything here is unresolved. Each entry states what is known, why it
matters, and how to close it. Items are ordered by impact on the rebuild.

**Status:** #1 (sensor access path), #2 (MCU revision) and #9 (relay GPIO
map) are **closed**. The USB device's role (a CDC-ACM virtual COM port)
and the panel's bus (SPI3) have also been established. The remaining
items are listed below with their current status.


---

## 1. How are the sensors reached? — **CLOSED**

**Known.** A corrected literal-pool census shows that **no I²C peripheral
is referenced anywhere in the image** — I2C1 (`0x40005400`) and I2C2
(`0x40005800`) have zero references, and no other I²C-like base exists.
The sensors are reached over three different paths, all recovered:

| Sensor | Path | Evidence |
|---|---|---|
| AT24C256 EEPROM | **software-bit-banged I²C on GPIOG** — SDA = PG6 (mask `0x40`), SCL = PG8 (mask `0x100`) | bit-bang primitives `0x08042A08`–`0x08042C88`, EEPROM API `0x08042938` (device byte `0xA0`, 32 KB limit), pins configured at `0x08008028` |
| NTC ×3, light sensor | **on-chip ADC at the AT32 address `0x40013C00`** | ADC HAL `0x08066B2C`–`0x08066D98`, read wrapper `0x08071178`, sampling `0x0806850C`; NTC results at `0x200001DE/E0/E2`, light at `0x20002248`+`0x278` |
| external NTC measurement device | **second two-wire serial bus on GPIOC** — data = PC8 (mask `0x100`), clock = PC9 (mask `0x200`), push-pull | primitives `0x08061958`–`0x08061B70`, user `0x08062504` (commands `0x88`/`0xFD`/`0x89`, 6 bytes read, NTC conversion `raw × 12500 / 65536 − 600`, clamped 100…9900 Ω) |

**Why it matters.** The rebuild's sensor HAL must implement a bit-banged
I²C master on PG6/PG8, an ADC driver for `0x40013C00`, and the GPIOC
serial protocol — none of which is a standard peripheral.

**Remaining sub-item — now closed.** The **S35390 RTC** and the **SI7021
humidity sensor** are *not* reachable over any bus the firmware uses, and
the writers of their values have now been found:

- the humidity byte at `0x20002248` + `0x291` is written by the
  protocol dispatcher at `0x080758E8` (three sites), by the control
  function at `0x0807F9BC` (two) and by `0x08080754` (one). The
  dispatcher is the function through which the Modbus and BACnet
  servers publish their points (it calls the point publisher
  `0x08042B6C`), so the humidity arrives **over the building bus**.
- the time is the date/time shadow at `0x200021C8`, whose offset `0x14`
  is written by `0x0807E260` — a function that also references the
  NTP server strings — so the clock is set from outside the local
  drivers as well.

So the two sensor drivers are compiled out for this product variant and
the values are supplied by the protocol slave path. The rebuild models
them accordingly: `firmware/src/drivers/rtc.c` and
`firmware/src/drivers/si7021.c` keep the S35390 and SI7021 register
models but bind them to the RAM shadow the firmware uses.

---

## 2. Which MCU revision? — **CLOSED: AT32F421, Cortex-M4 with FPU**

**Known.** The image executes ~780 VFP instructions (`vmov`, `vcvt`,
`vstr`, `vpush`, `vmul`, `vdiv`, `vcmp`). A Cortex-M3 has no FPU and
cannot execute them, so the part is **Cortex-M4F**. The ADC is used at
`0x40013C00`, the AT32 ADC address, not the STM32F1 ADC1 address
`0x40012400`; `0x40012404` is separately referenced once at `0x08066B18`,
so the two addresses are different AT32 peripherals. Together with the
`AT32F421` marking on the PCB this identifies the part as an
**AT32F421 (Cortex-M4, FPU)**.

**Consequences for the rebuild.** The current build targets
`-mcpu=cortex-m3 -mfloat-abi=soft`. That is now known to be wrong:

- the ABI must become `cortex-m4` + `hard` float (`-mfpu=fpv4-sp-d16`,
  `-mfloat-abi=hard`), matching the original's use of `s0`–`s31` and
  `d8`/`d9`;
- the register header must define the **AT32** peripheral map, at minimum
  the ADC at `0x40013C00`;
- SRAM/flash sizes should be re-checked against the AT32F421 data sheet
  (the initial SP `0x20016B20` still implies ≥93 KB of SRAM).

**How to close the remainder.** Confirm flash/SRAM sizes from the AT32F421
data sheet and switch the build; then re-verify the layout regression
(vector table at `0x08005800`, code at `0x08005950`).


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

## 5. Modbus register map and BACnet object table — **PARTLY CLOSED**

**Known.** A 26-entry point registry exists at `0x08090528`–`0x08090990`
as four-word descriptors `{name, label, value_or_RAM_address, handler}`.
Names are recovered; register addresses and BACnet object identifiers are
not.

**Why it matters.** Without the register map, a Modbus/BACnet client
cannot interoperate with the device.

**Now established.** The point registry's **layout** was recovered: the
entries at `0x08090528` are four-word records
`{name_ptr, label_ptr, value_or_RAM_address, handler_ptr}`, and the
complete published set is

| Name | Label | Value |
|---|---|---|
| `Actual_Room_temperature` | Room temperature | RAM |
| `Humidity` | Room relative humidity | RAM |
| `HeatSetTemp` | Heat Set Temp | RAM |
| `CoolSetTemp` | Cool Set Temp | RAM |
| `HoldEndtTime` | Hold End tTime | RAM |
| `HoldSetTemp` | Hold Set Temp | RAM |
| `HolidayStarttTime` | Holiday Start tTime | RAM |
| `HolidayEndtTime` | Holiday End tTime | RAM |
| `MaxSetTempLimit` | Max SetTemp Limit | RAM |
| `MinSetTempLimit` | Min SetTemp Limit | RAM |
| `W1relaystatus` … `Fanrelaystatus` | the five relay states | the GPIO |
| `Mode`, `HeatingStatus` | | |
| `FlagiSTouchPadLock`, `FlagiSHolidaying`, `FlagiSHolding` | the published flags | |
| `SysError`, `TempFormat`, `ProgramMode` | | |

The **Modbus register numbers** and the **BACnet object identifiers**
are still not recovered: the registry gives the names and the handlers,
but the tables that map them onto register numbers are built at run
time. The rebuild therefore defines its own mapping, exactly as the
existing Modbus server already does, and says so in
[`wire-formats.md`](wire-formats.md).

**How to close it.** Trace the register-number assignment in the Modbus
TCP server (referenced by `Modbus Tcp Server Start...`, `0x0806AC23`)
and the object-instance assignment in the BACnet server
(`bacnetip servo start...`, `0x0806B098`).

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

## 7. The display controller and its SPI command set — **PARTLY CLOSED**

**Known.** Panel is 480×320 colour over SPI. Framebuffer init byte
sequences are present (`0x36` MADCTL, `0x3A` PIXDIM, `0x11` SLPOUT,
`0x29` DISPON) but no controller name string exists.

**Now established.** The bus is **SPI3**, not SPI1: SPI3 has the
byte-wide write helper (`0x0807E730`, which writes SPI3->DR through the
pointer at `0x2000D61C`), the transfer routine (`0x0807EA2C`) and the
configuration registers (`0x0807E7D4`, `0x0807E8E8`), while SPI1 has
only two references and no traffic. The pixel format is 16 bits per
pixel, because the assets are stored as RGB565 in the NOR image.

**Still open:** the controller's part number. Its power-on sequence is
assembled in code rather than read from a table, so it cannot be
recovered statically.

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

## 9. Which GPIO drives which relay and sensor — **CLOSED for 5 of 6 relays**

**Known.** The point registry publishes five relay states; each handler
reads exactly one GPIO pin, which fixes the mapping directly:

| Relay | Terminal | GPIO | Handler |
|---|---|---|---|
| W1 | heat stage 1 | **PA3** | `0x08042674` |
| W2 | aux heat | **PA1** | `0x080426A8` |
| Y1 | cool stage 1 | **PB13** | `0x08013268` |
| Y2 | cool stage 2 | **PA2** | `0x0801329C` |
| Fan | G | **PB14** | `0x0803EA9C` |

The board output init at `0x0807DC5C` configures exactly `PA1`, `PA2`,
`PA3`, `PB13` and `PB14` as push-pull outputs and drives them high at
start-up — independent confirmation. The sensor pins are in
[hw-map.md](hw-map.md): EEPROM I²C on PG6/PG8, external measurement bus
on PC8/PC9.

**Still open.** The **O/B reversing valve** is not in the point registry,
so no handler names its pin. `firmware/src/board.c` keeps it `[I]`.
**How to close it:** trace the reversing-valve state in the HVAC code
(the state block at `0x20002248` carries the valve position) to the GPIO
write that drives it, then confirm against the schematic.


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

## 12. Behavioural verification against the original — **PARTLY CLOSED**

**Known.** There is no physical unit available, so the rebuild cannot be
compared against real hardware.

**Why it matters.** Every behavioural rule in
[hvac-spec.md](hvac-spec.md) started as an *inference* from strings and
plausible control flow.

**Now closed in software.** The **host-side differential harness**
exists and runs: `firmware/tests/differential.c` contains a *reference
model* — an independent transcription of the control rules in
[hvac-spec.md](hvac-spec.md), written without reference to the
implementation in `firmware/src/app/hvac.c` — and drives it and the
rebuild with the same deterministic stimulus matrix: every mode, every
equipment type, nine room temperatures, four setpoint pairs and the
occupancy, hold, holiday, frost, soak and defrost flags, in three
scenarios.

**82,944 cases, no divergence.** It found two real defects, both now
fixed:

1. the `1 Cool Stage` equipment type could never call for cooling;
2. the fan in the Fan and Ventilate modes was gated by the occupancy
   input, which the specification does not say — the occupancy input
   gates the compressor call, not the fan.

**Still open.**

1. **The transliteration of the original's own control function**
   (`0x0807F9BC`, 1249 instructions). This is the remaining half of the
   differential test and it needs the pseudocode transcribed by hand;
   the harness is ready for it — the reference model would be replaced by
   the transliteration.
2. **The physical unit**, when one becomes available: read the
   parameter block, log the relays over a controlled temperature sweep,
   and diff against the rebuild's model.

---

## 13. Lesser unknowns

- **Cloud endpoint paths.** Only `/HEader/SerialNumber/RoomDevices/
  RoomTemperature1/225` is known; the rest of the REST surface is not.
- **LoRaWAN payload format.** The AT command set is known, and the
  uplink is a byte string sent as hexadecimal (`AT+CTXP=%d`, with the
  modem's answers parsed with `%02x` and `%04x`), but the **field
  layout** is not: the payload is assembled at run time and no static
  table describes it. The rebuild defines its own sixteen-byte layout
  (`firmware/src/net/lora_payload.c`), marked as a rebuild choice in the
  same way the NTC temperature curve is.
- **JSON command grammar.** All keys are known; which are settable,
  their types and validation rules are not.
- **`MC6_E` / `MC6_2FCU` / `MC6_4FCU` variants.** These product-variant
  identifiers imply compile-time variants; which features each enables is
  unknown, and the firmware appears to support several hardware
  configurations from one binary.
- **Timezone/DST rules.** The zone list and `dst.bmp` are known; the
  transition algorithm is not.
- **The touch controller.** No controller name string exists and the
  firmware drives no I²C peripheral, so a capacitive controller would
  have to be bit-banged. The rebuild takes the touch input from the
  caller; the *touchpad lock* itself is recovered (the published flag
  `FlagiSTouchPadLock` at `0x08092CE4`, handler `0x0803EBC5`).
- **`0x08001258`–`0x08004FFF` (15,784 bytes)** is in neither input file.
  Whether it is reserved flash or an uncollected partition is unknown.
- **`0x08094238`–`0x080FFFFF`** (~460 KB) is likewise in neither file.
  The 9 code-referenced addresses in that range (up to `0x080EF04F`) are
  unresolvable without it.
