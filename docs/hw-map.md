# Hardware map

Everything in this document was established by resolving 32-bit constants
embedded in the shipped code (literal-pool loads via capstone), cross-checked
against the PCB photographs and the firmware's own string tables.

## MCU identification

**STM32F1xx-class MCU, ARMv7-M Cortex-M3 (Thumb-2), LQFP144.**
The PCB's large QFP is marked `AT32F421` — an Artery AT32 part, whose
peripheral map is F1-compatible. The firmware addresses peripherals using the
STM32F1 map exactly.

| Evidence | Value | Count |
|---|---|---|
| RCC base | `0x40021000` | 99 |
| FLASH base | `0x40022000` | 38 |
| GPIOA … GPIOG | `0x40010800` … `0x40012000` | 190 / 150 / 47 / 5 / 1 / 9 / 33 |
| USART1 / USART3 | `0x40013800` / `0x40004800` | 25 / 38 |
| UART4 / UART5 | `0x40004C00` / `0x40005000` | 17 / 51 |
| SPI1 / SPI3 | `0x40013400` / `0x40003C00` | 47 / 20 |
| TIM3 / TIM5 / TIM8 / TIM9-11 | `0x40000400` / `0x40000C00` / `0x40013400` / `0x40013C00` | 23 / 6 / 10 / 12 |
| USB block | `0x40005C40`–`0x40006000` | 47 |
| FSMC | `0xA0000000` | 4 |
| DMA1 | `0x40020000` | 4 |
| EXTI / AFIO | `0x40010400` / `0x40010000` | 12 / 15 |
| IWDG / RTC / PWR | `0x40003000` / `0x40002804` / `0x40007000` | 7 / 11 / 2 |

**Discriminators against STM32F4** (which is the main alternative): the F4
peripheral map was searched for and *not found* — RCC `0x40023800` (0 hits),
FLASH `0x40023C00` (0 hits), GPIOA `0x40020000` (4 hits, vs 190 for the F1
GPIOA `0x40010800`).

**No FPU.** `0xE000EF34` (FPCCR) does not appear in the image.

**Memory sizing.** The initial SP `0x20016B20` requires ≥93 KB of SRAM →
96 KB part. The image's RAM references top out at `0x20016B20`; the region
above `0x20018000` is unused.

### Peripherals *not* referenced

These F1 peripherals have **zero** literal-pool references:

`I2C1`, `I2C2`, `ADC1`, `ADC2/ADC3`, `TIM1`, `TIM2`, `TIM4`, `TIM6`,
`TIM7`, `CAN`, `BKP`, `DAC`, `WWDG`, `CRC`.

This is significant and contradicts an obvious reading of the hardware list.
The device *does* have an EEPROM (`AT24C256`), an RTC (`S35390`), a humidity
sensor (`SI7021`), three NTC thermistors and a light sensor — all of which
would conventionally sit on I²C or ADC. Their absence from the peripheral
map implies those buses are reached some other way. See
[open-questions.md](open-questions.md) — this is the single most important
unresolved hardware question, and it materially changes the HAL work.

*(Method note: an early census that reported these peripherals as "present"
was wrong because it bucketed constants by `value & 0xFFFFF000`, which
mishandles F1 bases whose low 12 bits are non-zero. The table above uses
correct nearest-base attribution.)*

## Terminals and relays

The PCB's terminal block silkscreen (photo 2) reads:

```
B  A  G  W2  W  Y2        Y1  O  R  Opd  Gnd  St
```

These match the firmware's *Terminal Definition* strings at
`0x08073DF4`–`0x08074208`:

| Terminal | Firmware string | Meaning |
|---|---|---|
| `W` | `Heating--W_OB` (`0x080741E7`) | Heat stage 1 |
| `W2` | `Aux Heating--W2_AUX` (`0x08073E1C`) | Heat stage 2 / auxiliary heat |
| `Y` | `1st compressor--Y` (`0x08073E78`) | Cool stage 1 |
| `Y2` | `2nd compressor--Y2` (`0x08073E64`) | Cool stage 2 |
| `G` | `Fan--G` (`0x08073DF4`) | Fan |
| `O/B` | `O/B Valve--W_OB` (`0x08073E50`) | Reversing (open/closed) valve |

So the board drives a **2-stage heat-pump / reversible HVAC system**:

```
   W   ── heat stage 1        (heating contactor)
   W2  ── heat stage 2 / aux heat
   Y   ── cool stage 1
   Y2  ── cool stage 2
   G   ── fan
  O/B  ── reversing valve
```

This is corroborated by the Modbus/BACnet point registry, which publishes
each relay individually:

| Point | Label | Handler |
|---|---|---|
| `W1relaystatus` | Heating relay W1 status | `0x08042674` |
| `W2relaystatus` | Heating relay W2 status | `0x080426A8` |
| `Y1relaystatus` | Cooling relay Y1 status | `0x08013268` |
| `Y2relaystatus` | Cooling relay Y2 status | `0x0801329C` |
| `Fanrelaystatus` | Fan relay G status | `0x0803EA9C` |

`firmware/src/board.c` currently holds **placeholder** port/pin assignments
for these six outputs (marked `[I]` — inferred). The correct mapping needs
the schematic.

## Display

- **Panel: 480 × 320, colour.** `bg.bmp` in the supplied icon set is exactly
  480×320, which fixes the geometry.
- **Interface: SPI.** SPI3 (`0x40003C00`) is referenced 20× and SPI1 47×; the
  display is driven over SPI with the panel carrying its own GRAM (a full
  480×320 RGB565 framebuffer is 300 KiB, far beyond the 96 KiB SRAM).
- **Controller type is not yet identified.** No `ILI9…`, `ST77…`, `SSD13…`,
  `R615…` or `NT35…` string appears in the firmware, and the framebuffer
  initialisation bytes (`0x36` MADCTL, `0x3A` PIXDIM, `0x11` SLPOUT,
  `0x29` DISPON) were found but only as raw byte sequences, not as an
  identified controller.

## External NOR flash (UI assets)

- **Part: SST39VF1601** — named verbatim in the firmware's self-test strings
  (`3. Norflash SST39VF1601 ok.` / `... error.`, `0x0801607C`).
- **Capacity: 16 Mbit = 2 MiB.** A 16 Mbit x8/x16 asynchronous NOR.
- **Interface: FSMC**, not SPI. The code at `0x080837C0` uses the classic F1
  FSMC bank-address idiom `sub.w r2, r2, #0x60000000`, i.e. the device is
  mapped at `0x60000000`. FSMC bases `0xA0000104` and `0xA0000220` are
  referenced at `0x0808379E`/`0x080837A8` and `0x08083892`/`0x080838A0`.
- **Filesystem: FAT32**, with OEM name `MSDOS5.0` and volume label `AT32` —
  both strings are present in the firmware at `0x080703C7` and `0x080703DC`,
  i.e. the firmware parses the boot sector of the on-board NOR.

The NOR holds the ~1,300 UI bitmaps referenced as
`\White\…`, `\Gray\…`, `\fan\…`, `\nfan\…`, `\Heatbar\…` plus ~290 root-level
names. See [assets.md](assets.md).

## Sensors and actuators

| Device | Firmware evidence | Bus (inferred) |
|---|---|---|
| AT24C256 EEPROM | `2. EEPROM AT24C256 ok.` / `... error.` (`0x08016048`) | see open questions |
| S35390 RTC | `4. RTC S35390 ok.` / `... error.` (`0x080160C3`) | see open questions |
| SI7021 humidity | `5. Humidity SI7021 fail` (`0x080160EB`) | see open questions |
| NTC ×3 | `7. On Board NTC: %d.%dc`, `8. In Door NTC`, `9. Out Door NTC` (`0x0801613F`+) | ADC or GPIO |
| Light sensor | `10. Light Sensor: %d` (`0x08016190`) | ADC or GPIO |
| Relay T1 | `1. Relay: T1_On` (`0x08016020`) | GPIO |

The boot-time self-test sequence is therefore: relay → EEPROM → NOR flash →
RTC → humidity → WiFi module → three NTC channels → light sensor. The
rebuild reproduces this ordering (`Hardware Checking...` at `0x08016013`).

## Wireless modules

- **ESP-07S** — Espressif ESP8266 with WiFi **and** Bluetooth LE. The
  firmware drives it over **USART1** (IRQ 37) with the Espressif AT command
  set (`AT+CWJAP?`, `AT+BLEINIT=`, `AT+BLUFI`/`AT+BLUFISEND`, `AT+GSN?`,
  `AT+GMR`). Self-test: `5. WIFI ESP-07S ok, v%s` (`0x0801610B`).
- **LoRaWAN modem** — a separate module on a second UART. The command set
  (`AT+DTRX=1,2,%d,`, `AT+CDEVEUI=`, `AT+CAPPEUI=`, `AT+CAPPKEY=`,
  `AT+CJOIN=`, `AT+CCLASS=2`, `AT+CTXP=`, `AT+CADR=`, `AT+CFREQBANDMASK=`)
  is the **Dragino/Queclink-style LoRaWAN modem** AT interface. Menu strings
  confirm the full parameter set: `SF7`–`SF12`, `BW125`, `Enable ADR`/
  `Disable ADR`, transmit power `3dBm`–`17dBm`.
- The two modules are also reflected in the `MC6_E` / `MC6_TIMER` /
  `MC6_2FCU` / `MC6_4FCU` / `MC6_HW` product-variant identifiers at
  `0x08092D6C`–`0x08092D98`.

## Board identification

PCB silkscreen (photo 2): `MC6-U-2-L 20241001`.
Firmware identity strings: `MC6_E`, `MC6_TIMER`, `MC6_2FCU`, `MC6_4FCU`,
`MC6_HW`; OTA path `/ftp/Products/MC6-U-Lora/FalconHP_Mark/` (`0x0808AAC3`);
version format `U-LW-%d.%02d` (`0x0803431C`); build stamp `20260812`
(`0x0806565C`); JSON version `5.0.4` (`0x0807D8EC`).

The input file is named `…Mark-261001`, i.e. the **2026-10-01** Mark of the
`FalconHP` product line.
