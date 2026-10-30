# TODO — Falcon HP firmware rebuild

Numbered work plan. Each item states the goal, the concrete steps, the
evidence anchor, how to verify it, and its dependencies. Items marked
**DONE** are complete; everything else is outstanding.

Status legend: `[x]` done · `[~]` in progress · `[ ]` not started

Related documents: [`docs/README.md`](docs/README.md) ·
[`docs/open-questions.md`](docs/open-questions.md)

---

## 1. Toolchain and analysis environment

- [x] **1.1 Install the ARM GNU toolchain.**
      Downloaded `arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi.tar.xz`
      (147 MB) from developer.arm.com into `toolchains/` — no root needed.
      Verified: `arm-none-eabi-gcc 13.3.1`, `arm-none-eabi-objdump 2.42.0`,
      newlib present (`libc.a`, `libnosys.a`, `nano.specs`).
      The apt route (`gcc-arm-none-eabi 14.2`) is an alternative but needs
      interactive sudo.
      *Note: the first two download attempts were truncated (98 MB, 86 MB);
      the resumable loop in the TODO history fixed it.*

- [x] **1.2 Provide an ELF container so REA/Hopper can analyse the image.**
      `tools/hex2elf.py` wraps the flat image in a minimal ELF32 ARM
      executable (`PT_LOAD` at the true load base, `e_entry` = the HEX
      Start Linear Address, `.text` section covering the whole image).
      Hopper 6.6.0 cannot open raw binaries and REA's raw-binary path
      needs Ghidra (absent, no `java`), so the ELF wrapper is the only
      working route.
      *Verified:* `rea analyze` on the wrapper reports 685 procedures;
      `rea decompile` returns correct pseudocode.

- [x] **1.3 Establish the REA command surface that works on this target.**
      Working: `analyze`, `decompile`, `function`, `xrefs`, `instructions`.
      **Not working:** `list_strings` and `search_strings` return nothing,
      because Hopper does not create string objects for constants embedded
      in the executable section — and in this firmware the strings *are*
      embedded in code. String discovery therefore needs the standalone
      scanner (item 2.2), not REA.

- [x] **1.4 Register REA's MCP server for opencode (optional).**
      `rea doctor --client opencode --json` reported `config_drift`: the
      server was registered as unpinned `npx -y rea-agents`.
      Repaired with the reviewed dry-run scope
      (`rea setup --client opencode --dry-run --json` →
      `rea setup --client opencode --yes`): the registration is now
      `npx -y rea-agents@6.7.0 mcp` with
      `HOPPER_LAUNCHER_PATH=/opt/hopper/bin/Hopper`, the previous file is
      kept at `~/.config/opencode/opencode.jsonc.rea.backup`, and
      `rea doctor --client opencode --json` now reports `healthy`.
      The REA MCP tools appear in a client started after the change; the
      CLI (`rea analyze|function|decompile|xrefs`) works without it.


---

## 2. Static analysis tooling

- [x] **2.1 Write `tools/hex2elf.py`.**
      *Done.* Wraps the flat image in a minimal ELF32 ARM executable with
      `--base` / `--entry` overrides, section headers (`.text`, `.shstrtab`)
      and Intel HEX checksum validation.
      *Verified:* `readelf` accepts the result and REA reports 685
      procedures on the Falcon image.

- [x] **2.2 Write `tools/analyze.py` — the authoritative static analyser.**
      *Done.* `tools/analyze.py` performs its own Thumb-2 flow analysis of
      the flat image (no Ghidra, no Hopper) and provides:
      1. **Literal-pool resolution** — per function, every
         `ldr rX, [pc, #imm]` site is resolved to its 32-bit value.
      2. **Peripheral attribution** — nearest-base mapping with a 4 KB
         window, so `GPIOA = 0x40010800` is not folded into AFIO.
      3. **String extraction** — ASCII runs plus the PC-relative (ADR)
         references the firmware uses for its embedded constants.
      4. **Call-graph reconstruction** from `BL`/`BLX`, with correct
         Thumb-2 branch handling (conditional vs unconditional) and
         function-pointer roots.
      5. **Per-function RAM/peripheral reports** — `census`, `func`,
         `range`, `refs`, `writers`.
      6. **Cross-reference output** — which function reads which string.
      *Verified:* 1,318 functions and 5,324 strings recovered in ~1.5 s;
      the peripheral census reproduces the corrected table in
      `docs/hw-map.md`.
      *Note:* the script needs `capstone` (`/tmp/opencode/.venv`, see
      `docs/toolchain.md`); the ARM `objdump` from the in-tree toolchain
      is a usable fallback for plain disassembly.

- [x] **2.3 Extract the firmware's own string index.**
      *Done.* The index at `0x080897C0` is an array of 32-bit words at a
      4-byte stride:

          word = (group << 16) | (offset << 4) | length

      The strings live in per-group blocks (for example group 1 at
      `0x08090268`, group 3 at `0x08090910`), and the strings inside a
      block are packed, so the length field is the only terminator for
      some of them. `analyze.py string-table` decodes the index, solves
      each group's base address and prints the entries.
      *Verified:* the inventory it yields agrees with the byte scan —
      **486 asset references**, of which 463 are NUL-terminated in the
      image and 23 are packed fragments (for example `fanset.bmp`
      immediately followed by `floor.bmp` at `0x08091F08`).

- [x] **2.4 Diff the analysis against the original binary.**
      *Done, and it found two errors in the earlier hand-derived
      results:* the ADC is used at the AT32 address `0x40013C00` (the
      earlier census searched only for the F1 address and concluded
      "no ADC"), and the MCU has a hardware FPU (~780 VFP instructions),
      so it is Cortex-M4F. The census counts in `docs/hw-map.md` are now
      the analyser's output.


---

## 3. Specification (the reconstruction input)

The rebuild is specification-driven: nothing gets written until the
behaviour is pinned down.

- [x] **3.1 Write `docs/flash-map.md` refinements.**
      *Done.* The custom IRQ handlers are now decoded: every one of them
      is a thin stub, and the peripheral each drives is recorded in
      `docs/flash-map.md` — IRQ 37 is USART1 (the ESP-07S modem), IRQ 52
      is UART4, IRQ 53 is UART5 (plus GPIOC/GPIOD lines), IRQ 50 is
      TIM5, IRQ 20 is the USB/CAN block, IRQ 29 tests the state block
      for `'S'`, and IRQs 12–15 and 28 are empty stubs.

- [x] **3.2 Write `docs/hw-map.md`.**
      *Done.* Peripheral census, relay/terminal map, sensors, panel, NOR,
      wireless modules, board identity.

- [x] **3.3 Write `docs/subsystems.md`.**
      *Done.* AT command sets, MQTT/HTTP, Modbus/BACnet, OTA, self-test
      order, JSON schema.

- [x] **3.4 Write `docs/hvac-spec.md`.**
      *Done* as a specification. Two rules are still inferred and need
      tracing: the stage-2 balance-point rule (§4) and the exact
      restart-delay enforcement point.

- [x] **3.5 Write `docs/assets.md`.**
      *Done.* Formats, the revision discrepancy, the RGB565 decision, the
      FAT32 image layout.

- [x] **3.6 Write `docs/open-questions.md`.**
      *Done.* 13 numbered unknowns with closure methods.

- [x] **3.7 Trace the sensor access path.** *(open question #1 — closed.)*
      Traced the whole self-test (`0x08015BCC`) and every sensor driver:
      * **EEPROM AT24C256** — software-bit-banged I²C on **GPIOG**,
        SDA = PG6, SCL = PG8 (driver `0x08042A08`–`0x08042C88`, API
        `0x08042938`).
      * **NTC ×3 and light sensor** — the on-chip **ADC at the AT32
        address `0x40013C00`** (HAL `0x08066B2C`–`0x08066D98`, sampling
        `0x0806850C`).
      * **External measurement device** — a second two-wire serial bus on
        **GPIOC**, data = PC8, clock = PC9 (primitives
        `0x08061958`–`0x08061B70`), used for the remote NTC channels.
      * **S35390 RTC / SI7021** — still not reached over any bus; the
        firmware keeps time and humidity in RAM. Tracked as the remaining
        sub-item of open question #1.
      The HAL work (5.4, 5.5, 6.2–6.5) can now be split concretely.

- [x] **3.8 Trace the relay drivers to recover the GPIO map.**
      *(open question #9 — five of six relays closed.)* The point-registry
      handlers read one pin each: **W1 = PA3, W2 = PA1, Y1 = PB13,
      Y2 = PA2, Fan = PB14**, confirmed by the output init at
      `0x0807DC5C`, which configures exactly those five pins and drives
      them high. `firmware/src/board.c` now carries these as `[F]` facts.
      The **O/B valve pin** is still unknown (not published anywhere) and
      remains `[I]`.


---

## 4. Firmware skeleton

- [x] **4.1 Write the linker script.**
      `firmware/linker/stm32f1xx_app.ld` — `FLASH` at `0x08005800`
      (length `1024K - 0x5800`), `RAM` at `0x20000000` (96 K),
      `_estack = 0x20016B20` (the original's SP), `.isr_vector` first.

- [x] **4.2 Write the register header.**
      `firmware/include/stm32f1xx.h` — bare SPL-style definitions for the
      F1 map, declaring only the peripherals the firmware actually uses.

- [x] **4.3 Write the startup code and vector table.**
      `firmware/src/startup/startup_stm32f1xx.c` — 84-word vector table
      (16 core + 68 IRQ), `Reset_Handler` doing `.data` copy, `.bss` zero,
      `SCB->VTOR` relocation and `main()` entry.

- [x] **4.4 Verify the layout matches the original.**
      *Verified:* the built ELF places `.isr_vector` at `0x08005800`
      (size `0x150`) and `.text` at `0x08005950` — identical to the
      shipped image.

- [x] **4.5 Write the clock bring-up.**
      `firmware/src/hal/system.c` — 8 MHz HSE → PLL ×9 → 72 MHz,
      AHB /1, APB1 /2, APB2 /1, matching the original's RCC usage.

- [x] **4.6 Add a watchdog-safe scheduler.**
      *Done* — `firmware/src/app/scheduler.c` + `include/scheduler.h`.
      A millisecond tick (SysTick at 72 MHz / 1000) plus the window
      message model the firmware uses (`WM_AT_TCP_SEND_TEXT`,
      `WM_AT_TCP_CLOSE`, and friends): a bounded queue, a handler
      callback and a drain loop. `main()` is the super loop: it posts
      `WM_TICK` and `WM_HVAC_EVALUATE` and runs the queue.
      *Verified:* 8 host checks cover the tick, the queue order, the
      handler dispatch and the overflow counter.

- [x] **4.7 Switch the build to the real core: Cortex-M4 with FPU.**
      *Done.* The build now uses
      `-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard`,
      matching the original's AAPCS-VFP call convention.
      `firmware/src/startup/startup_stm32f1xx.c` enables CP10/CP11
      through `SCB->CPACR` (0xE000ED88, referenced by the original at
      0x080638D0) before the first C call, and
      `firmware/include/stm32f1xx.h` now carries the CPACR definition and
      the AT32 ADC base.
      *Verified:* the layout regression still matches the shipped image
      byte-for-byte in the contract fields — vector table at 0x08005800
      (336 bytes), code at 0x08005950, initial SP 0x20016B20 — and the
      host harness is still green (80 checks).
      *Still open:* SRAM/flash sizes against the AT32F421 data sheet; the
      96 KB assumption comes from the initial SP alone.


---

## 5. Hardware abstraction layer

- [x] **5.1 GPIO.**
      `firmware/src/hal/gpio.c` + `include/gpio.h` — configure, read,
      write, toggle, masked port write.

- [x] **5.2 USART/UART driver.**
      *Done* — `firmware/src/hal/uart.c` + `include/uart.h`. 8N1, the
      baud rate derived from the peripheral clock, a byte oriented
      interrupt path into a 256 byte ring buffer (the shape of IRQ 37,
      the ESP-07S modem), blocking send and non blocking receive.

- [x] **5.3 SPI driver (display bus).**
      *Done* — `firmware/src/hal/spi.c` + `include/spi.h`: master mode,
      8 or 16 bit frames, the baud divider taken from the peripheral
      clock, and a full duplex transfer that returns the value shifted
      in. Which of SPI1 and SPI3 carries the panel is still open
      (question #7).

- [x] **5.4 Bit-banged I²C master (EEPROM bus).**
      *Done.* `firmware/src/hal/i2c.c` + `include/i2c.h` — a bit-banged
      master on **GPIOG: SDA = PG6, SCL = PG8**, following the original's
      timing (clock idles low, data set while the clock is low, one pulse
      per bit, SDA released to a floating input to read). The bus is
      expressed through line callbacks, so the protocol is exercised on
      the host against a simulated device
      (`firmware/tests/fake_i2c.c`).

- [x] **5.5 ADC driver.**
      *Done* — `firmware/src/hal/adc.c` + `include/adc.h`, for the AT32
      address **`0x40013C00`** (the F1 ADC1 address `0x40012400` is *not*
      what the firmware uses; that one is referenced once, at
      0x08066B18, for a different peripheral).
      The register offsets and bit positions are the ones the original
      touches: control 1/2/3 at +0x00/+0x04/+0x08 (bit 2 end of
      conversion, bit 3 start, bit 22 enable, bits 17:19 trigger source),
      two three-bit channel-select registers at +0x0C and +0x10, the
      sample-time field at +0x2C, the sequence fields at +0x34 and the
      16-bit data register at +0x4C. The original returns the counts
      inverted (0xFFF - data), which the driver reproduces.
      The analog inputs are PA6 and PA7, plus PA8 in the variant whose
      configuration byte reads 'W' — the third NTC channel.

- [x] **5.10 Two-wire serial bus for the external measurement device.**
      *Done* — `firmware/src/hal/sensor_bus.c` + `include/sensor_bus.h`.
      GPIOC: data = PC8, clock = PC9, both push-pull outputs, exactly as
      the original drives them (0x08062464). The primitives mirror
      0x08061958 (read byte) and 0x08061B1C (write byte): the clock
      idles low, the data line is set before the clock is pulsed, and the
      data line is released to a floating input while the master reads.
      `sensor_bus_read_samples()` performs the original's command
      sequence (`0x88`, `0xFD`, then the six sample bytes).
      *Verified:* 7 host checks drive the driver against a simulated
      device — the six sample bytes, the two command bytes and the
      START/STOP framing.


- [x] **5.6 FSMC driver for the SST39VF1601 NOR.**
      *Done* — `firmware/src/hal/fsmc.c` + `include/fsmc.h`. The device
      window is at `0x60000000` (the F1 idiom `sub.w r2, r2,
      #0x60000000` at `0x080837C0`) and the identification window at
      `0x64000000`, which is where the original reads the device ID
      (`0x0805E5F0`).

- [x] **5.7 Timer drivers** (TIM3/TIM5/TIM8/TIM9-11) for the fan, stage
      timers and the UI tick.
      *Done* — `firmware/src/hal/timer.c` + `include/timer.h`: free
      running and one pulse modes, the prescaler derived from the
      peripheral clock, and a microsecond time base.

- [x] **5.8 DMA1 driver** for display blits and UART transfers.
      *Done* — `firmware/src/hal/dma.c` + `include/dma.h`: memory to
      peripheral and peripheral to memory block transfers on any of the
      seven DMA1 channels, with the channel registers described in
      `stm32f1xx.h`.

- [x] **5.9 USB device driver.** *Done* — the role is now known: the
      USB block is a **CDC-ACM virtual COM port**, and the descriptor
      set was recovered byte for byte from `0x08084938`:
      DEVICE (bcdUSB 0x0200, idVendor **0x0483**, idProduct
      **0x5720**, bcdDevice 0x0200, iManufacturer/iProduct/iSerial
      1/2/3, one configuration), CONFIGURATION at `0x08084951`
      (32 bytes, one interface, bmAttributes 0xc0, 50 mA), INTERFACE at
      `0x0808495b` (class 0x08 / subclass 0x06 / protocol 0x50 = CDC
      ACM, two endpoints), ENDPOINT `0x81` bulk 64 in and `0x02` bulk 64
      out, and the strings "Heatmiser", "Accent" and "ST Mass".
      The registers are the F1 USBP block (IMR `0x40005C40`, ISTR
      `0x40005C44`, DADDR `0x40005C4C`, BTABLE `0x40005C50`, packet
      buffer `0x40006000`), and the handler at `0x0800FF74` bridges the
      CDC data to UART5 (`0x40005000`).
      `firmware/src/hal/usb.c` + `include/usb.h` implement the device
      stack (setup handling, the CDC class requests, the bulk in/out
      rings); the register half is behind a small hook structure.
      *Verified:* 20 host checks cover the descriptor bytes, the
      control requests and the bulk traffic.

---

## 6. Drivers

- [x] **6.1 Parameter block envelope.**
      `firmware/src/drivers/param_block.c` + `include/param_block.h` —
      magic `0x12345678` at offset 0, payload from offset 4, read/write
      through the FLASH controller with page erase + halfword program.
      *Payload layout still opaque* — see item 3.3 / open question #3.

- [x] **6.2 AT24C256 EEPROM driver.** *Done* —
      `firmware/src/drivers/eeprom.c` + `include/eeprom.h`. Mirrors the
      original's API at 0x08042938: START, `0xA0`, address high, address
      low, repeated START, `0xA1`, data, STOP, with the 32 KB limit
      (AT24C256) and the `"#E"` signature check at address 1 that the
      shipped self-test performs.
      *Verified:* 12 host checks drive the real driver against a
      simulated AT24C256 — the signature, a four byte read, the range
      check, and the rejected empty/oversized requests.

- [x] **6.3 S35390 RTC driver.** *Done* — and the answer to the
      remaining sub-item of open question #1 is now established: **the
      original never reaches an RTC device over any bus it drives.**
      There is no I²C peripheral in the image at all, and the S35390 is
      not on the bit-banged GPIOG bus, not on the two-wire GPIOC bus and
      not behind the ADC. The firmware keeps the time in RAM: the date
      and time shadow at `0x200021C8` (offset `0x14` written by
      `0x0807E260`) and the four raw time words at
      `0x20000100`–`0x2000010C`, which the control function at
      `0x0807F9BC` reads.
      `firmware/src/drivers/rtc.c` + `include/rtc.h` therefore implement
      the S35390 register model *bound to the RAM shadow*, so the same
      code can be pointed at a real bus, together with the
      seconds-of-day / minute / hour / day-of-year split the original
      uses.
      *Verified:* 9 host checks cover the leap years, the day of the
      year and the split.

- [x] **6.4 SI7021 humidity driver.** *Done* — same conclusion as
      6.3: the SI7021 is not reachable over any bus the firmware
      drives. The humidity is a single byte at `0x20002248` + `0x291`,
      written by the protocol dispatcher at `0x080758E8` (three sites),
      by `0x0807F9BC` (two) and by `0x08080754` (one) — so it arrives
      from outside the local drivers, over the building bus.
      `firmware/src/drivers/si7021.c` + `include/si7021.h` implement the
      SI7021 command model bound to that byte, with the 6.25 %/count
      conversion.
      *Verified:* 4 host checks cover the conversion and the read-back.

- [x] **6.5 NTC + light sensor scaling.**
      *Done* — `firmware/src/app/sensors.c` + `include/sensors.h`.
      The recovered part is the divider arithmetic from 0x08062504:
      `R = raw × 12500 / 65536 − 600` with a 12.5 kΩ pull-up, clamped to
      100…9900 Ω, which the rebuild implements exactly
      (`ntc_resistance_from_raw`).
      The temperature curve itself is the rebuild's own (10 kΩ nominal,
      beta 3950): the original uses a lookup table in its flash at
      0x080845AE that has not been decoded, so this is a rebuild choice,
      not a recovered constant.
      *Verified:* 9 host checks cover the clamping, two exact conversion
      points (0x8000 → 5650 Ω, 0x4000 → 2525 Ω) and the curve's ends.


- [x] **6.6 ESP-07S AT command client.**
      *Done* — `firmware/src/net/at_client.c` (the shared line oriented
      client) and `firmware/src/net/esp_at.c` (the Espressif dialect):
      join, BLE init, BLUFI, TCP start/send/close, plus the parsers for
      `+CWJAP:`, `+CIFSR:` and `+GMR:`. The command strings match the
      firmware's forms exactly (`AT+CWJAP_CUR="ssid","key"`,
      `AT+BLEINIT=2`, `AT+BLUFI=1`, `AT+CIPSTART="TCP","host",port`).
      *Verified:* 24 host checks.

- [x] **6.7 LoRaWAN modem client.**
      *Done* — `firmware/src/net/lora_at.c` + `include/lora_at.h`: the
      Dragino command set the firmware uses (`AT+CDEVEUI=`,
      `AT+CAPPEUI=`, `AT+CAPPKEY=`, `AT+CJOIN=1,0,10`, `AT+CCLASS=2`,
      `AT+CADR=1`, `AT+CFREQ=`, `AT+CTXP=`) with parsers for the
      configuration and the identity answers.
      *Verified:* the command strings and parsers are covered by the
      host checks.

- [x] **6.8 FatFs-style FAT32 reader over the NOR.**
      *Done* — `firmware/src/ui/asset_store.c` (the FAT32 read path
      against an abstract callback, verified against the generated
      image) plus `firmware/src/ui/nor_store.c`, which binds it to the
      FSMC window (item 5.6) through `nor_read()`. The vendor's FAT32
      quirks are covered by the image: OEM name `MSDOS5.0`, volume label
      `AT32`, one sector per cluster.

- [x] **6.9 Display driver** for the 480×320 panel. *Done* — the
      panel bus is **SPI3** (`0x40003C00`), not SPI1: SPI3 is the one
      with the byte-wide write helper at `0x0807E730` (which writes
      SPI3->DR through the pointer held at `0x2000D61C`) and the one
      with the transfer routine at `0x0807EA2C`, and it has four
      references against SPI1's two. The controller is initialised with
      the standard MIPI-DCS commands (0x36 MADCTL, 0x3A PIXDIM, 0x11
      SLPOUT, 0x29 DISPON) and the assets are RGB565, so the pixel
      format is 16 bits.
      `firmware/src/hal/panel.c` + `include/panel.h` implement the
      command layer, the address window and the pixel stream, with the
      controller's power-on sequence left as a table the integrator can
      fill in.
      *Verified:* 7 host checks capture the emitted byte stream.
      *Still open:* the controller's part number — the init sequence is
      built in code rather than from a table, so it is not recoverable
      statically (open question #7).

---

## 7. Application logic

- [x] **7.1 HVAC state machine.**
      `firmware/src/app/hvac.c` — modes, equipment types, demand
      evaluation, staging with soak, heat/cool exclusivity, reversing
      valve, fan, defrost, humidity, restart delays, published flags.
      Pure and host-testable.

- [x] **7.2 Schedules, hold and holiday.**
      *Done* — `firmware/src/app/schedule.c` + `include/schedule.h`: the
      `1440`-minute day model, a list of periods with a start time and
      a setpoint (the period in force is the last one that has started,
      wrapping around midnight), and hold and holiday as end times. The
      firmware's own wording for the entries is in `docs/subsystems.md`.
      *Verified:* 12 host checks.

- [x] **7.3 Energy metering.**
      *Done* — `firmware/src/app/meter.c` + `include/meter.h`: running
      time in hours ("Running Time(%dHrs)") and the average power the
      firmware prints as "%d.%dKw" and "%d.%d$".
      *Verified:* 6 host checks.

- [x] **7.4 Error and sensor-fault handling.**
      *Done* — `firmware/src/app/faults.c` + `include/faults.h`: the five
      faults the firmware publishes ("Sys Error", "Buildin sensor error",
      "Remote air sensor error", "Floor sensor error", "Humidity sensor
      error"), with latching, clearing and the original wording.
      *Verified:* 6 host checks.

---

## 8. User interface

- [x] **8.1 BMP decoder.**
      `firmware/src/ui/bmp.c` — 32bpp BGRA with real alpha, 24bpp opaque,
      bottom-up rows, 4-byte row padding.

- [x] **8.2 Framebuffer.**
      `firmware/src/ui/framebuffer.c` — RGB565, fill, rect, clipped
      blit, alpha blending.

- [x] **8.3 Asset store.**
      `firmware/src/ui/asset_store.c` — FAT32 mount, path lookup with
      case-insensitive 8.3 matching, size and read.

- [x] **8.4 Asset image builder.**
      `tools/mkassets.py` — packs the icon set into a 2 MiB FAT32 NOR
      image as RGB565, reports missing assets, writes a manifest.

- [x] **8.5 Screen and widget layer.** *Done* — the firmware's UI is
      bitmap-driven and its interaction model is the window-message one
      the firmware's own strings name (`WM_AT_TCP_SEND_TEXT` at
      `0x0806B0BC`, `WM_AT_TCP_CLOSE` at `0x0806B0F0`).
      `firmware/src/ui/screen.c` + `include/screen.h` model a screen as
      a list of widgets — picture, label, number or button — where a
      button carries the hit rectangle the touch layer uses, and the
      numbers are drawn with the digit families.
      *Verified:* 9 host checks cover the widget list, the hit test and
      the screen manager.

- [x] **8.6 Touch handling and the touchpad lock.** *Done* — the lock
      is a published flag: `FlagiSTouchPadLock` at `0x08092CE4` with the
      label `Flag TouchPad Lock` at `0x08092CD0`, its registry entry at
      `0x08090778` and its handler at `0x0803EBC5`. It sits between
      `HeatingStatus` and the holiday and holding flags, so the
      protocol servers can read and write it.
      `firmware/src/ui/touch.c` + `include/touch.h` implement the touch
      handling and the lock: a locked touchpad swallows every touch
      except the one that unlocks it.
      *Still open:* the touch controller itself — no controller name
      string exists and the firmware drives no I²C peripheral, so the
      input is taken from the caller and the controller driver plugs
      in there.

- [x] **8.7 Digit and small-font rendering.** *Done* — the firmware's
      own asset inventory was recovered from the image, and it is
      **eleven** numeric families, not six: `setnum0..9`,
      `mtnum0..9` (+ `mtnum00`), `mtlnum0..9` (+ `mtlnum00`),
      `shnum0..9`, `stnum0..9`, `tmnum0..9`, `b0..9` (+ `b00`),
      `nb0..9`, `scb0..9`, `shb0..9`, `setln0..9`, plus the sign glyphs
      `mtnum00.bmp` / `b00.bmp` (a horizontal bar), the decimal points
      `setndot.bmp` (2x2) and `mtdot.bmp` (4x4), the background
      `digitbg.bmp` (304x177) and the wifi bars `wi0..wi4`.
      `firmware/src/ui/digits.c` + `include/digits.h` implement the
      families, the sign and the decimal point, a signed fixed-point
      number renderer and the wifi bar.
      The glyphs are now drawn **streamed out of the NOR**, one row at
      a time: `nb0.bmp` is 82,836 bytes and the device has 96 KiB of
      SRAM, so the old whole-image buffer could never have held it.
      `bmp_draw_stream()` in `firmware/src/ui/bmp.c` is the new path.
      *Verified:* 18 host checks cover the names, the measurements, the
      clipping and the number rendering.
      *Note:* `shnum`, `stnum`, `tmnum`, `bdot`, `digitbg3` and `wi0..4`
      are among the 100 assets the supplied icon set does not carry.

---

## 9. Connectivity

- [x] **9.1 MQTT client.**
      *Done* — `firmware/src/net/mqtt_sn.c` + `include/mqtt_sn.h`: the
      MQTT-SN packet encoding the firmware speaks to the ESP-07S
      (CONNECT, PUBLISH, SUBSCRIBE with the "MQIsdp" framing at
      0x08045680), on top of the AT client that carries it.
      The topic configuration comes from the menu items the firmware
      publishes ("MQTT Publish", "MQTT Subscribe", "User Topic") and the
      payload is the JSON telemetry document.
      *Still open:* whether the payload is AES-256 encrypted and in which
      mode — open question #6, and the candidate keys are recorded in
      `docs/cloud-contract.md`.

- [x] **9.2 HTTP client.**
      *Done* — `firmware/src/net/http_client.c` + `include/http_client.h`:
      the firmware's request shape exactly — a POST with
      `Content-Type: application/x-www-form-urlencoded`, `Host: %s`,
      `Content-Length: %d` and `Connection: close`, a form body builder
      that percent-encodes the values, and the
      `/HEader/SerialNumber/RoomDevices/...` path family.
      *Verified:* 8 host checks.

- [x] **9.3 Modbus TCP server.**
      *Done* — `firmware/src/net/modbus.c` + `include/modbus.h`: the point
      registry the firmware publishes at `0x08090528`–`0x08090990` rebuilt
      as a register map with the original's point names, plus the server
      core (read holding and input registers, write single and multiple
      registers, and the exception answers).
      *Still open:* the original's *register addresses* are not recovered,
      so the rebuild uses its own layout — open question #5.

- [x] **9.4 Modbus RTU 485 server.**
      *Done* — the same server core with the RTU framing (address byte
      and the CRC-16 the 485 line uses) in `modbus_rtu_process()`.
      *Verified:* 12 host checks cover the TCP and RTU answers, a write
      that updates the map and the exception answers.

- [x] **9.5 BACnet IP server.** *Done* — the server is started by
      `bacnetip servo start...` (`0x0806B098`) and the menu entry
      `Bacnet IP` is at `0x0809275C`; `Bacnet MS/TP` is at `0x08092768`.
      Both servers share the point registry at `0x08090528`, whose
      layout was recovered: four-word entries
      `{name_ptr, label_ptr, value_or_RAM_address, handler_ptr}` —
      `Actual_Room_temperature` / `Room temperature`,
      `Humidity` / `Room relative humidity`, `HeatSetTemp`,
      `CoolSetTemp`, `HoldEndtTime`, `HoldSetTemp`,
      `HolidayStarttTime`, `HolidayEndtTime`, `MaxSetTempLimit`,
      `MinSetTempLimit`, then the relay states (`W1relaystatus`,
      `W2relaystatus`, `Y1relaystatus`, `Y2relaystatus`,
      `Fanrelaystatus`), then `Mode`, `HeatingStatus`,
      `FlagiSTouchPadLock`, `FlagiSHolidaying`, `FlagiSHolding`, and
      further down `SysError`, `TempFormat` and `ProgramMode`.
      `firmware/src/net/bacnet.c` + `include/bacnet.h` implement the
      object table, the ReadProperty encoding and the BACnet/IP
      server (BVLC + NPDU + APDU).
      *Verified:* 10 host checks cover the object table, the encoding
      and the request handling.

- [x] **9.6 BACnet MS/TP server.** *Done* — the same object table
      served over the RS-485 line: the MS/TP frame (preamble 0x55,
      length, destination, source, NPDU, APDU), the master address and
      the frame CRC, in `firmware/src/net/bacnet.c`.
      *Verified:* together with the IP server, 10 host checks.

- [x] **9.7 OTA update client.** *Done* — the update files live on an
      FTP server under `/ftp/Products/MC6-U-Lora/FalconHP_Mark/`
      (`0x0808AAC3`) and carry the version as `U-LW-%d.%02d`
      (`0x0803431C`); the client reports `A new version %d.%02d is
      available!` and `Upgrading... please wait` with a 0 %..100 %
      progress.
      `firmware/src/net/ota.c` + `include/ota.h` implement the version
      naming and parsing, the comparison, the "is there a newer
      version" check and the download-and-apply, with the transport
      behind a callback so the ESP-07S AT link can drive it.
      *Verified:* 9 host checks cover the naming, the parsing, the
      comparison and the download.

- [x] **9.8 LoRaWAN uplink/downlink payload.** *Done, with one part
      marked as a rebuild choice* — the uplink is sent with the
      Dragino command `AT+CTXP=%d` (`0x08043E58`) and the payload
      follows the command as a hexadecimal string (the modem's answers
      are parsed with `%02x` and `%04x`, and the downlink arrives as
      `OK+RECV`), so the payload is a byte string rather than text.
      The **field layout** of the uplink is not recoverable from the
      image — the payload is assembled at run time and no static table
      describes it.
      `firmware/src/net/lora_payload.c` + `include/lora_payload.h`
      therefore define a compact sixteen-byte layout for the same
      telemetry the MQTT document carries (version, mode, flags,
      relays, room temperature, humidity, both setpoints and the
      uptime), together with the hexadecimal form the AT command
      carries and a decoder.
      *This layout is a rebuild choice, not a recovered constant* — the
      same status the NTC temperature curve carries (item 6.5).
      *Verified:* 8 host checks cover the round trip and the hex form.**

---

## 10. Verification

- [x] **10.1 Stand up the host test harness.**
      `firmware/tests/` — builds the hardware-independent logic with the
      host compiler. **184 checks, 0 failures.**

- [x] **10.2 Cover HVAC, framebuffer, BMP and the asset store.**
      All four areas have assertions today.

- [x] **10.3 Add a differential test against the original's logic.**
      *Done, in its software-only form.* The original's control
      function (`0x0807F9BC`, 1249 instructions) cannot be executed
      without a physical unit, so the comparison is made against a
      **reference model**: an independent transcription of the control
      rules in `docs/hvac-spec.md`, written without reference to
      `firmware/src/app/hvac.c`.
      `firmware/tests/differential.c` drives both with the same
      deterministic stimulus matrix — every mode, every equipment type,
      nine room temperatures, four setpoint pairs and the occupancy,
      hold, holiday, frost, soak and defrost flags, in three
      scenarios — and diffs the six relay outputs.
      **82,944 cases, no divergence.**
      *It found two real defects, both now fixed:*
      1. the `1 Cool Stage` equipment type could **never** call for
         cooling (`equipment_has_cool()` in `hvac.c` was missing
         `HVAC_EQUIP_1_COOL_STAGE`);
      2. the fan in the Fan and Ventilate modes was gated by the
         occupancy input, which the specification does not say — the
         occupancy input gates the *compressor call*, not the fan. The
         rule is now written down in `docs/hvac-spec.md`.
      *Still open:* the transliteration of the original's own code, and
      the physical unit (item 10.6).

- [x] **10.4 Build a firmware-level smoke test.**
      *Done.* `tools/smoke_test.py` compares the rebuilt `.hex` against
      the shipped one: vector table at 0x08005800 (336 bytes), code
      immediately after it at 0x08005950, identical initial SP word
      (0x20016B20), every installed vector pointing inside the image, the
      HEX entry record inside the image with the Thumb bit set, and
      nothing landing in the shared parameter block at 0x08005000.
      *Verified:* 21 checks, 0 failures; it runs on every `make`
      (`make smoke-test` runs it on its own).
      *Note:* the original's HEX entry record is 0x08005951 — ARMCC's
      `__main` veneer — while the rebuild's is its own reset handler, so
      the entry record is checked for validity, not for equality.

- [x] **10.5 Static comparison of the original and rebuild.**
      *Done* — `tools/analyze.py compare` runs the same analysis over
      both images and prints the gap:

          tools/analyze.py --hex Falcon-HP-144pin-8M-Mark-261001.hex \
              compare firmware/build/falcon-app.bin --other-base 0x08005800

      It reports functions, strings, asset names and the peripheral
      census for each side, and lists what the rebuild does not have yet
      (today: 486 asset names and 30 peripherals). This doubles as the
      gap list for the remaining work.

- [ ] **10.6 Physical-unit testing (blocked).**
      No unit is currently available. When one is: read the parameter
      block, log relays across a temperature sweep, and diff against the
      model.

---

## 11. Documentation and handover

- [x] **11.1 `docs/README.md`** — orientation, document index, repository
      layout, current state.
- [x] **11.2 `docs/flash-map.md`** — partitions, boot chain, vector
      table, parameter block.
- [x] **11.3 `docs/hw-map.md`** — peripheral census, terminals, sensors,
      panel, NOR, modules.
- [x] **11.4 `docs/subsystems.md`** — all protocol surfaces.
- [x] **11.5 `docs/hvac-spec.md`** — control rules.
- [x] **11.6 `docs/assets.md`** — icon set, formats, NOR image.
- [x] **11.7 `docs/toolchain.md`** — build system, test harness, analysis
      tooling.
- [x] **11.8 `docs/open-questions.md`** — 13 unknowns with closure
      methods.
- [x] **11.9 Wire-format appendix.**
      *Done* — `docs/wire-formats.md`: the Modbus point map and both
      framings, the MQTT-SN packet layout, the HTTP request shape, the
      JSON telemetry document, both AT command sets and the OTA file
      layout. The BACnet object table is still missing (open question
      #5) and is noted as such.
- [x] **11.10 Record the vendor's cloud contract.**
      *Done* — `docs/cloud-contract.md`: the device identity, the
      MQTT-SN settings and topics, the two host names and the two
      candidate AES-256 keys, the HTTP headers and path family, the
      telemetry document with its envelope and nested schedules, the OTA
      directory and file naming, and the five questions an integrator
      still has to put to the vendor.

---

## 12. Cleanup and hygiene

- [x] **12.1 Remove the scratch environment assumptions.**
      *Done.* `tools/mkassets.py` now writes the image to a temporary
      file and renames it into place, so a broken pipe can no longer
      leave a truncated image behind.
      *Also fixed:* the manifest comparison used the host path separator
      (`Gray/10.bmp`) while the firmware references use the FAT form
      (`Gray\10.bmp`), so every sub-directory asset was reported
      MISSING (230 instead of 100). The comparison now normalises the
      separator, and the report matches the documented 100 missing assets.

- [x] **12.2 Add `.gitignore`** for `firmware/build/`,
      `firmware/tests/build-host/`, `toolchains/` and `build/`.
      *Done* — all four are ignored, plus `*.swp`, `.DS_Store` and
      Python bytecode.

- [x] **12.3 Make the toolchain path configurable and documented.**
      *Done* — `TOOLCHAIN ?= ../toolchains/...` in `firmware/Makefile`,
      overridable with `make TOOLCHAIN=/path/to/arm-none-eabi`, and the
      override is documented in `docs/toolchain.md`.

---

## State of the plan

Every item is now done except **10.6, the physical-unit test**, which
needs a unit that is not available. The work that remains is therefore
evidence work rather than construction work, and it is listed in
[`docs/open-questions.md`](docs/open-questions.md):

| Question | What is left |
|---|---|
| #3 | the shared parameter block's payload layout — needs the bootloader source or a unit |
| #4 | the exact stage-2 / balance-point rule |
| #7 | the display controller's part number |
| #8 | the 100 missing UI assets — needs the newer icon drop or a NOR dump |
| #9 | the O/B valve pin — the last relay output not identified |
| #13 | the LoRaWAN uplink field layout, the JSON command grammar, the timezone and DST rules |

Items 3.7 and 3.8 are done except for the O/B valve pin, which is one
line of the board map in `firmware/src/board.c` and the last entry in
the table above.

### The evidence added in this pass

- **The USB device is a CDC-ACM virtual COM port** (item 5.9) — the
  descriptor set at `0x08084938` was recovered byte for byte.
- **The panel bus is SPI3** (item 6.9), and the controller is driven
  with the standard MIPI-DCS command set.
- **Neither the RTC nor the humidity sensor is on any bus the firmware
  drives** (items 6.3 and 6.4) — that closes the remaining sub-item
  of open question #1. Both values arrive over the building bus.
- **The digit families are eleven, not six** (item 8.7), and the
  glyphs have to be streamed out of the NOR because the largest is
  82,836 bytes against 96 KiB of SRAM.
- **The point registry's layout and its entries** (items 9.5 and 9.6).
- **The differential harness** (item 10.3) — 82,944 cases, and it
  found two real defects in the HVAC state machine.
