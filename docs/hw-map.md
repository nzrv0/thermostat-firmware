# Hardware map

Everything in this document was established by resolving 32-bit constants
embedded in the shipped code (literal-pool loads via capstone), cross-checked
against the PCB photographs and the firmware's own string tables.

## MCU identification

**AT32F421-class MCU, ARMv7-M Cortex-M4 (Thumb-2), FPU, LQFP144.**
The peripheral map is STM32F1-compatible except for the ADC, which sits at
the AT32 address; the PCB's large QFP is marked `AT32F421`.

Three independent observations fix the core as **Cortex-M4 with a
hardware FPU**:

1. The image contains ~780 VFP instructions (`vmov` 404, `vldr` 192,
   `vstr` 75, `vcvt` 37, `vpop`/`vpush` 37, `vmul` 9, `vdiv` 6, `vcmp` 20).
   A Cortex-M3 has no FPU and cannot execute them at all.
2. The ADC is used at **`0x40013C00`** — the AT32 ADC address — not at the
   STM32F1 ADC1 address `0x40012400`. The AT32 ADC driver is at
   `0x08066B2C`–`0x08066D98` (channel select at `+0x10`, external-trigger
   and mode bits at `+0x08`, 16-bit data register at `+0x4C`).
3. `0x40012404` (F1 ADC1 + CR1) *is* referenced once, at `0x08066B18`, so
   the two addresses belong to different peripherals of the AT32 map.

| Evidence | Value | Count |
|---|---|---|
| RCC base | `0x40021000` | 10 |
| FLASH base | `0x40022000` | 7 |
| GPIOA … GPIOG | `0x40010800` … `0x40012000` | 28 / 32 / 17 / 4 / 1 / 5 / 11 |
| USART1 / USART3 | `0x40013800` / `0x40004800` | 5 / 12 |
| UART4 / UART5 | `0x40004C00` / `0x40005000` | 5 / 12 |
| SPI1 / SPI3 | `0x40013400` / `0x40003C00` | 2 / 4 |
| TIM3 / TIM5 | `0x40000400` / `0x40000C00` | 7 / 2 |
| ADC (AT32 address) | `0x40013C00` | 2 |
| USB block | `0x40005C40`–`0x40006000` | 28 |
| FSMC | `0xA0000000` | 2 |
| DMA1 / DMA2 | `0x40020000` / `0x40020400` | 1 / 4 |
| EXTI / AFIO | `0x40010400` / `0x40010000` | 2 / 2 |
| IWDG / PWR | `0x40003000` / `0x40007000` | 1 / 1 |


*Discriminators against STM32F4* (the main alternative): the F4 peripheral
map was searched for and *not found* — RCC `0x40023800` (0 hits), FLASH
`0x40023C00` (0 hits), GPIOA `0x40020000` (4 hits, vs 21 for the F1
GPIOA `0x40010800`).

**Memory sizing.** The initial SP `0x20016B20` requires ≥93 KB of SRAM.
The image's RAM references top out at `0x20016B20`; the region above
`0x20018000` is unused.

### Peripherals *not* referenced

These F1 peripherals have **zero** literal-pool references:

`I2C1`, `I2C2`, `ADC1` (at the F1 address `0x40012400`), `ADC2/ADC3`,
`TIM1`, `TIM2`, `TIM4`, `TIM6`, `TIM7`, `CAN`, `BKP`, `DAC`, `WWDG`, `CRC`.

The absence of **I²C1/I²C2** is the key result: the sensors are not on a
hardware I²C bus. The absence of ADC1 is now explained — the ADC is used,
but at the AT32 address `0x40013C00`; the earlier census searched for the
STM32F1 address only.

*(Method note: an early census that reported these peripherals as "present"
was wrong because it bucketed constants by `value & 0xFFFFF000`, which
mishandles F1 bases whose low 12 bits are non-zero. The table above uses
correct nearest-base attribution. A second, earlier census reported "zero
ADC references" because it assumed the STM32F1 ADC address.)*


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

| Point | Label | Handler | GPIO [F] |
|---|---|---|---|
| `W1relaystatus` | Heating relay W1 status | `0x08042674` | **PA3** |
| `W2relaystatus` | Heating relay W2 status | `0x080426A8` | **PA1** |
| `Y1relaystatus` | Cooling relay Y1 status | `0x08013268` | **PB13** |
| `Y2relaystatus` | Cooling relay Y2 status | `0x0801329C` | **PA2** |
| `Fanrelaystatus` | Fan relay G status | `0x0803EA9C` | **PB14** |

Each of those handlers loads the port base from a literal pool
(`0x40010800` = GPIOA, `0x40010C00` = GPIOB), masks one pin and calls the
GPIO read helper at `0x08071C54`, so the pin assignment is direct evidence,
not inference.

The board-level output init at `0x0807DC5C` (called from the init chain
`0x0807F46C`) configures **exactly** `PA1`, `PA2`, `PA3`, `PB13` and `PB14`
as push-pull outputs and then drives all five **high** through the BSRR —
independent confirmation of the same five pins, and evidence that the
outputs are active-high.

The **O/B valve** is driven by the firmware but is *not* published in the
point registry, so no handler names its pin. It is still `[I]`.

## Board GPIO configuration

Recovered from every call to the GPIO configuration helper `0x08071BAC`
(struct: `u16 pin_mask`, `u8 pull`, `u8 mode`, `u8 cnf`, `u8 speed`;
the nibble written into CRL/CRH is `mode` when `cnf == 0`, otherwise
`(cnf & 8) | (pull & 4) | (speed & 3)`).

| Port | Pins | Mode | Init function |
|---|---|---|---|
| GPIOA | PA1, PA2, PA3 | output push-pull | `0x0807DC5C` (relays) |
| GPIOA | PA5 | input / output | `0x080103A0`, `0x080103CC` |
| GPIOA | PA8 | output | `0x080658FC` |
| GPIOA | PA9 | alternate function | `0x08044950` |
| GPIOA | PA10 | AF open-drain | `0x08044950` |
| GPIOA | PA11, PA12 | AF (USB DM/DP) | `0x0806234C` |
| GPIOB | PB1 | output | `0x08074310` |
| GPIOB | PB10, PB15 | alternate function | `0x08048670`, `0x08060D9C` |
| GPIOB | PB11 | AF open-drain | `0x08048670`, `0x08060D9C` |
| GPIOB | PB12 | output | `0x080658FC` |
| GPIOB | PB13, PB14 | output push-pull | `0x0807DC5C` (relays) |
| GPIOC | PC0 | output | `0x080448F8` |
| GPIOC | **PC8** | output | `0x08062464` (sensor data) |
| GPIOC | **PC9** | output | `0x08062464` (sensor clock) |
| GPIOC | PC10 | alternate function | `0x08065A18` |
| GPIOC | PC11 | input | `0x08065A18` |
| GPIOC | PC12 | alternate function | `0x08065128` |
| GPIOD | PD2 | input | `0x08065128` |
| GPIOD | PD3 | output | `0x0800FDB0` |
| GPIOD | PD0, PD1, PD4, PD5, PD12–PD15 | AF | `0x08070F48` |
| GPIOE | PE7–PE15 | AF | `0x08070F48` |
| GPIOF | PF0 | AF | `0x08070F48` |
| GPIOF | PF9 | input | `0x0805F098` |
| GPIOF | PF11 | output | `0x08072CBC` |
| GPIOG | **PG6** | output | `0x08008028` (I²C SDA) |
| GPIOG | **PG8** | output | `0x08008028` (I²C SCL) |


## Display

- **Panel: 480 × 320, colour.** `bg.bmp` in the supplied icon set is exactly
  480×320, which fixes the geometry.
- **Interface: SPI.** SPI3 (`0x40003C00`) is referenced 4× and SPI1
  (`0x40013400`) 2×; the display is driven over SPI with the panel
  carrying its own GRAM (a full 480×320 RGB565 framebuffer is 300 KiB,
  far beyond the 96 KiB SRAM). PA11/PA12 are configured as alternate
  function at `0x0806234C`, which is the USB DM/DP pair, so they are not
  the panel bus.
- **The panel bus is SPI3.** SPI3 carries the byte-wide write helper at
  `0x0807E730` — it writes SPI3->DR through the pointer held at
  `0x2000D61C` — and the transfer routine at `0x0807EA2C`, with the
  configuration registers at `0x0807E7D4` and `0x0807E8E8`. SPI1 has
  only two references and no traffic.
- **Controller type is not yet identified.** No `ILI9…`, `ST77…`, `SSD13…`,
  `R615…` or `NT35…` string appears in the firmware, and the framebuffer
  initialisation bytes (`0x36` MADCTL, `0x3A` PIXDIM, `0x11` SLPOUT,
  `0x29` DISPON) were found but only as raw byte sequences, not as an
  identified controller. The controller's power-on sequence is assembled
  in code rather than read from a table, so it is not recoverable
  statically (open question #7). The rebuild's
  `firmware/src/hal/panel.c` therefore implements the standard MIPI-DCS
  subset the firmware uses and leaves the controller-specific sequence
  as a table the integrator can fill in.
- **Pixel format: RGB565.** The assets are stored as 16 bits per pixel
  in the NOR image (see [assets.md](assets.md)), and the panel's own
  GRAM is addressed with the standard 16-bit window commands.

## USB

The USB block is **a CDC-ACM virtual COM port**, not a DFU or a HID
device. The descriptor set at `0x08084938` was recovered byte for byte:

| Descriptor | Address | Content |
|---|---|---|
| DEVICE | `0x08084938` | bcdUSB 0x0200, class 0/0/0, bMaxPacketSize0 64, idVendor **0x0483**, idProduct **0x5720**, bcdDevice 0x0200, iManufacturer 1, iProduct 2, iSerialNumber 3, one configuration |
| CONFIGURATION | `0x08084951` | 9 bytes, wTotalLength 32, one interface, value 1, bmAttributes 0xc0, bMaxPower 0x32 (50 mA) |
| INTERFACE | `0x0808495B` | number 0, two endpoints, class **0x08** (Communications), subclass **0x06** (ACM), protocol **0x50** (CDC) |
| ENDPOINT | `0x0808495C` | `0x81` bulk, 64 bytes, interval 0 |
| ENDPOINT | `0x08084963` | `0x02` bulk, 64 bytes, interval 0 |
| STRING | `0x0808496C` | "Heatmiser" (1), "Accent" (2), "ST Mass" (3) |

The identifiers `0x0483`/`0x5720` are STMicroelectronics' virtual COM
port pair, so the firmware ships the standard ST VCP descriptor set
with its own strings.

The registers are the F1 USBP block: **USR_IMR `0x40005C40`**,
**USR_ISTR `0x40005C44`**, **USR_DADDR `0x40005C4C`**, **USR_BTABLE
`0x40005C50`** and the packet buffer at `0x40006000`. The interrupt
handler at `0x0800FF74` bridges the CDC data to **UART5**
(`0x40005000`) through the buffers at `0x200002A4`–`0x200002F8`, so the
console is a USB-to-UART bridge.

## External NOR flash (UI assets)

- **Part: SST39VF1601** — named verbatim in the firmware's self-test strings
  (`3. Norflash SST39VF1601 ok.` / `... error.`, `0x0801607C`).
- **Capacity: 16 Mbit = 2 MiB.** A 16 Mbit x8/x16 asynchronous NOR.
- **Interface: FSMC**, not SPI. The code at `0x080837C0` uses the classic F1
  FSMC bank-address idiom `sub.w r2, r2, #0x60000000`. FSMC bases
  `0xA0000104` and `0xA0000220` are referenced at `0x0808379E`/`0x080837A8`
  and `0x08083892`/`0x080838A0`.
- **Device window at `0x64000000`.** The self-test's NOR check at
  `0x0805E5F0` writes the SST identification sequence to
  `0x6400AAAA` (`0xAA`), `0x64005554` (`0x55`) and `0x6400AAAA` (`0x90`),
  then reads the manufacturer and device ID back from `0x64000000`
  (`+0x00`, `+0x02`, `+0x1C`, `+0x1E`). The self-test compares the first
  halfword with `0xBF` (SST manufacturer ID) and the second with `0x234B`
  (`"K#"`), and finally writes `0xAA` / `0x55` / `0xF0` to leave the ID
  mode. The window base is therefore `0x64000000`, 64 MiB above the F1
  FSMC base of `0x60000000` — the board's address lines are wired so the
  device sits in that bank.
- **Filesystem: FAT32**, with OEM name `MSDOS5.0` and volume label `AT32` —
  both strings are present in the firmware at `0x080703C7` and `0x080703DC`,
  i.e. the firmware parses the boot sector of the on-board NOR.


The NOR holds the ~1,300 UI bitmaps referenced as
`\White\…`, `\Gray\…`, `\fan\…`, `\nfan\…`, `\Heatbar\…` plus ~290 root-level
names. See [assets.md](assets.md).

## Sensors and actuators

| Device | Firmware evidence | Bus [F] |
|---|---|---|
| AT24C256 EEPROM | `2. EEPROM AT24C256 ok.` / `... error.` (`0x08016048` / `0x08016060`) | **bit-banged I²C on GPIOG: SDA = PG6, SCL = PG8** |
| S35390 RTC | `4. RTC S35390 ok.` / ` 4. RTC S35390 error.` (`0x080160D4` / `0x080160BB`) | not reached over a bus — see below |
| SI7021 humidity | ` 5. Humidity SI7021 fail` (`0x080160EB`) | not reached over a bus — see below |
| NTC ×3 | `7. On Board NTC: %d.%dc`, `8. In Door NTC`, `9. Out Door NTC` (`0x0801613F`+) | **MCU ADC at `0x40013C00`** (values at RAM `0x200001DE/E0/E2`, tenths °C) |
| Light sensor | `10. Light Sensor: %d` (`0x08016190`) | **MCU ADC at `0x40013C00`** (value at RAM `0x20002248` + `0x278`) |
| Relay T1 | `1. Relay: T1_On` (`0x08016020`) | GPIO |

The boot-time self-test sequence is therefore: relay → EEPROM → NOR flash →
RTC → humidity → WiFi module → three NTC channels → light sensor. The
rebuild reproduces this ordering (`Hardware Checking...` at `0x08016013`).

### The three sensor paths, in detail

**1. EEPROM — software-bit-banged I²C on GPIOG.** The driver at
`0x08042A08`–`0x08042C88` is a textbook bit-bang implementation: send-byte
(`0x08042BD8`) shifts the byte out MSB-first, driving **SCL = PG8**
(mask `0x100`) and **SDA = PG6** (mask `0x40`) through the GPIO helpers
`0x08071AA8` (BSRR, set high) and `0x08071AAC` (BRR, set low), with
`0x08042C88` as the delay loop. The EEPROM API at `0x08042938` refuses
addresses ≥ `0x8000` (32 KB = AT24C256) and sends the device address byte
`0xA0`; the self-test reads two bytes and expects `"#E"`.

**2. NTC and light sensor — the on-chip ADC.** The ADC is used at the AT32
address `0x40013C00`. The read wrapper at `0x08071178` configures a channel
through the HAL-like code at `0x08066BF C` (`0x08066BFC`) and reads the
16-bit data register at `+0x4C`. The sampling routine at `0x0806850C`
converts the counts with a lookup table in flash at `0x080845AE` and stores
the three NTC temperatures as signed tenths of a degree at `0x200001DE`,
`0x200001E0`, `0x200001E2`; the light sensor lands at `0x20002248` + `0x278`.

**3. External measurement device — a second two-wire bus on GPIOC.**
`0x08061958` (read byte), `0x08061B1C` (write byte), `0x08061A50` /
`0x080619BC` (start/stop), `0x08061A80` / `0x08061AE0` (acknowledge) drive
**PC8 = data** (mask `0x100`) and **PC9 = clock** (mask `0x200`), both
configured as push-pull outputs by `0x08062464`. `0x08062504` sends the
command bytes `0x88`, `0xFD`, polls with `0x89` (up to 1000 times) and then
reads six bytes — three 16-bit samples. The conversion applied to them
(`raw × 12500 / 65536 − 600`, clamped to 100…9900 Ω) is the classic
NTC-divider formula with a 12.5 kΩ pull-up, so this bus feeds the *remote*
NTC channels through an external 16-bit ADC.

**Bus timing recovered from the original.** Both buses idle the clock low
and set the data line *before* the clock is pulsed, MSB first; the master
releases the data line to a floating input whenever it reads. On the
EEPROM bus the acknowledge slot is clocked but its result is ignored by
the original (`0x08042A08` returns a sample count that the caller
discards) — the rebuild checks it instead. The GPIO helpers are
`0x08071AA8` = BRR (drive low) and `0x08071AAC` = BSRR (drive high), with
the data and clock masks `0x40` and `0x100` on GPIOG.


**What is still open.** The S35390 RTC and the SI7021 humidity sensor are
not reachable over any bus the firmware uses: no I²C peripheral is
referenced, the GPIOG bus is used only by the EEPROM driver, and the GPIOC
bus only by the measurement device. In this build the firmware keeps time
in RAM (date/time shadow at `0x200021C8`, raw time words at
`0x20000100`–`0x2000010C`, state block at `0x20002248`) and the humidity
as one byte at `0x20002248` + `0x291` (copied to `0x20001469` for the UI).
The self-test has an RTC "ok" path but only a humidity *fail* message.
Two readings are possible — the sensors are read by a driver that is
compiled out for this variant (`MC6_2FCU` / `MC6_4FCU` build flags), or
they arrive over the building-bus (Modbus) rather than locally — and
neither is established yet.


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
