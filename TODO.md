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

- [ ] **1.4 Register REA's MCP server for opencode (optional).**
      `rea doctor --client opencode` reports `config_drift`: the server is
      not registered in `~/.config/opencode/opencode.jsonc`.
      Only needed for in-session MCP tools; the CLI already works.
      Steps: `rea setup --client opencode --dry-run --json` → review →
      `rea setup --client opencode --yes` → restart the client.

---

## 2. Static analysis tooling

- [x] **2.1 Write `tools/hex2elf.py`.**
      *Done.* Wraps the flat image in a minimal ELF32 ARM executable with
      `--base` / `--entry` overrides, section headers (`.text`, `.shstrtab`)
      and Intel HEX checksum validation.
      *Verified:* `readelf` accepts the result and REA reports 685
      procedures on the Falcon image.

- [ ] **2.2 Write `tools/analyze.py` — the authoritative static analyser.**
      This is the highest-leverage analysis tool because REA/Hopper cannot
      do string discovery here. It must provide:
      1. **Literal-pool resolution** — for every function, find the
         `ldr rX, [pc, #imm]` sites and resolve the 32-bit values.
         This is what produced the peripheral census.
      2. **Peripheral attribution** — map each resolved constant to the
         owning peripheral using *nearest-base* attribution.
         *Trap to avoid:* bucketing by `value & 0xFFFFF000` is wrong,
         because F1 peripheral bases have non-zero low 12 bits
         (`GPIOA = 0x40010800` → bucket `0x40010000` = AFIO). An early
         census was wrong for exactly this reason and had to be redone.
      3. **String extraction** — recover NUL-terminated strings *and* the
         FAT-style path forms (`\White\105.bmp`), keeping the leading
         backslash and the 4-byte-aligned padding the firmware uses.
      4. **Call-graph reconstruction** from `BL` targets, with the
         correct Thumb-2 immediate encoding (`S:I1:I2:imm10:imm11`).
      5. **Per-function RAM/peripheral usage reports** — the basis for the
         hardware map.
      6. **Cross-reference output** — which function reads which string.

- [ ] **2.3 Extract the firmware's own string index.**
      The image contains a packed string table using a
      `u16 (offset<<4 | length, group)` encoding around `0x080897C0`
      (it decodes correctly for `pair1.bmp`, `te\12.bmp`, `Tues`,
      `SF8,BW125`, `+4.5`). Fully decode it: it is the authoritative
      string inventory and would confirm the 486-asset reference count
      independently of the regex scan.
      *Anchor:* `0x080897C0`.

- [ ] **2.4 Diff the analysis against the original binary.**
      Once 2.2 exists, cross-check its output against the hand-derived
      results already recorded in `docs/hw-map.md` (peripheral census,
      486 assets, 68 IRQ slots). Any disagreement means one of the two is
      wrong and must be resolved.

---

## 3. Specification (the reconstruction input)

The rebuild is specification-driven: nothing gets written until the
behaviour is pinned down.

- [ ] **3.1 Write `docs/flash-map.md` refinements.**
      *Partially done.* Remaining: document the 13 custom IRQ handlers'
      decoded behaviour (which peripheral each drives) — see
      `docs/open-questions.md` §10.

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

- [ ] **3.7 Trace the sensor access path.** *(open question #1 — highest
      impact.)* Follow the EEPROM/RTC/humidity/NTC self-test functions
      back to their register accesses and determine whether the buses are
      bit-banged, front-ended, or delegated to the ESP-07S. This decides
      the entire sensor HAL.

- [ ] **3.8 Trace the relay drivers to recover the GPIO map.**
      *(open question #9.)* Decompile the handlers behind
      `W1relaystatus` (`0x08042674`), `W2relaystatus` (`0x080426A8`),
      `Y1relaystatus` (`0x08013268`), `Y2relaystatus` (`0x0801329C`),
      `Fanrelaystatus` (`0x0803EA9C`) and read out the port addresses and
      pin masks.

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

- [ ] **4.6 Add a watchdog-safe scheduler.**
      Needs the tick source and main-loop structure
      *(open question #11)*. The original appears to be a bare-metal
      super-loop with a `WM_*` window-message model.

---

## 5. Hardware abstraction layer

- [x] **5.1 GPIO.**
      `firmware/src/hal/gpio.c` + `include/gpio.h` — configure, read,
      write, toggle, masked port write.

- [ ] **5.2 USART/UART driver.**
      Five UARTs (USART1/2/3, UART4/5) with a byte-oriented
      interrupt path, matching IRQ 37's observed behaviour.

- [ ] **5.3 SPI driver (display bus).**
      SPI1 and SPI3, master mode, 8/16-bit frames, DMA capable.

- [ ] **5.4 I²C or bit-bang sensor bus.**
      Blocked on item 3.7.

- [ ] **5.5 ADC driver.**
      Blocked on item 3.7 — may not exist at all.

- [ ] **5.6 FSMC driver for the SST39VF1601 NOR.**
      The original uses the F1 idiom `sub.w r2, r2, #0x60000000`
      (`0x080837C0`); reproduce that, with the NOR on external bus bank 1.

- [ ] **5.7 Timer drivers** (TIM3/TIM5/TIM8/TIM9-11) for the fan, stage
      timers and the UI tick.

- [ ] **5.8 DMA1 driver** for display blits and UART transfers.

- [ ] **5.9 USB device driver.** USB is referenced 47×; its role (CDC
      console? DFU?) is unknown.

---

## 6. Drivers

- [x] **6.1 Parameter block envelope.**
      `firmware/src/drivers/param_block.c` + `include/param_block.h` —
      magic `0x12345678` at offset 0, payload from offset 4, read/write
      through the FLASH controller with page erase + halfword program.
      *Payload layout still opaque* — see item 3.3 / open question #3.

- [ ] **6.2 AT24C256 EEPROM driver.** Blocked on item 3.7.

- [ ] **6.3 S35390 RTC driver.** Blocked on item 3.7.

- [ ] **6.4 SI7021 humidity driver.** Blocked on item 3.7.

- [ ] **6.5 NTC + light sensor scaling.** Blocked on item 3.7.

- [ ] **6.6 ESP-07S AT command client.**
      Line-oriented parser, command/response correlation, the full command
      set from `docs/subsystems.md`, plus BLE provisioning (BLUFI).

- [ ] **6.7 LoRaWAN modem client.**
      Dragino-style AT set, ADR, SF/BW/power configuration.

- [ ] **6.8 FatFs-style FAT32 reader over the NOR.**
      *Substantially done:* `firmware/src/ui/asset_store.c` implements the
      FAT32 read path against an abstract read callback and is verified
      against the generated image. It still needs the FSMC transport
      wired in (item 5.6) and the vendor's FAT32 quirks checked.

- [ ] **6.9 Display driver** for the 480×320 panel.
      Blocked on open question #7 (controller identification).

---

## 7. Application logic

- [x] **7.1 HVAC state machine.**
      `firmware/src/app/hvac.c` — modes, equipment types, demand
      evaluation, staging with soak, heat/cool exclusivity, reversing
      valve, fan, defrost, humidity, restart delays, published flags.
      Pure and host-testable.

- [ ] **7.2 Schedules, hold and holiday.**
      `temp_prog` / `timer_prog` structures, `1440`-minute day model,
      hold/holiday end times, timezone and DST.

- [ ] **7.3 Energy metering.**
      `Running Time(%dHrs)`, `Power consumption(%d.%dKw)`, `Power
      consumption(%d.%d$)` (`0x08081C50`–`0x08081C80`).

- [ ] **7.4 Error and sensor-fault handling.**
      `Sys Error`, `Buildin sensor error`, `Remote air sensor error`,
      `Floor sensor error`, `Humidity sensor error`
      (`0x08092E7C`–`0x08092ECC`).

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

- [ ] **8.5 Screen and widget layer.**
      The firmware's UI is bitmap-driven with digit fonts; the screen
      inventory is large (~1,300 assets). Reconstruct the screen graph
      from the asset names and the `WM_*` window messages.

- [ ] **8.6 Touch handling and the touchpad lock.**
      `Flag TouchPad Lock` is a published flag; the lock behaviour and
      the touch controller are unknown.

- [ ] **8.7 Digit and small-font rendering.**
      `setnum0..9`, `mtnum0..9`, `mtlnum0..9`, `shnum0..9`, `stnum0..9`,
      `tmnum0..9`, `wi0..4`, `digitbg.bmp`.

---

## 9. Connectivity

- [ ] **9.1 MQTT client.**
      MQTT-SN over the ESP-07S, topic configuration, the JSON telemetry
      document, AES-256 payload handling *(open question #6)*.

- [ ] **9.2 HTTP client.**
      `Content-Type: application/x-www-form-urlencoded`, the
      `Header/SerialNumber/RoomDevices` path family.

- [ ] **9.3 Modbus TCP server.**
      Point registry at `0x08090528`–`0x08090990` is the starting point
      *(open question #5)*.

- [ ] **9.4 Modbus RTU 485 server.**

- [ ] **9.5 BACnet IP server.**
      `bacnetip servo start...` (`0x0806B098`).

- [ ] **9.6 BACnet MS/TP server.**

- [ ] **9.7 OTA update client.**
      FTP upload to `/ftp/Products/MC6-U-Lora/FalconHP_Mark/`, the
      `U-LW-%d.%02d` version scheme, progress reporting.

- [ ] **9.8 LoRaWAN uplink/downlink payload.**

---

## 10. Verification

- [x] **10.1 Stand up the host test harness.**
      `firmware/tests/` — builds the hardware-independent logic with the
      host compiler. **43 checks, 0 failures.**

- [x] **10.2 Cover HVAC, framebuffer, BMP and the asset store.**
      All four areas have assertions today.

- [ ] **10.3 Add a differential test against the original's logic.**
      The decisive verification step, and it needs **no hardware**:
      transliterate the original's control functions to C (from REA
      pseudocode), then drive both implementations with identical input
      sequences and diff the relay outputs. This converts every inferred
      rule in `docs/hvac-spec.md` into an observed one.
      *(open question #12.)*

- [ ] **10.4 Build a firmware-level smoke test.**
      Assert the built `.hex` vector table, entry point and partition
      placement against the original on every build — an automated
      regression guard on the layout contract.

- [ ] **10.5 Static comparison of the original and rebuild.**
      Same peripheral usage census, same string inventory, same protocol
      command sets, produced by `tools/analyze.py` against both binaries.

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
- [ ] **11.9 Wire-format appendix.**
      Once the Modbus/BACnet/JSON/LoRa formats are traced, add a
      normative appendix so integrators can interoperate.
- [ ] **11.10 Record the vendor's cloud contract.**
      Endpoints, topics, encryption, and the OTA file layout.

---

## 12. Cleanup and hygiene

- [ ] **12.1 Remove the scratch environment assumptions.**
      `tools/mkassets.py` must not be piped to `head` — it dies on SIGPIPE
      mid-write and leaves a truncated image. Documented in
      `docs/toolchain.md`; add a guard in the script.

- [ ] **12.2 Add `.gitignore`** for `firmware/build/`,
      `firmware/tests/build-host/`, `toolchains/` and `build/`.

- [ ] **12.3 Make the toolchain path configurable and documented.**
      Currently `TOOLCHAIN ?= ../toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi`.

---

## Suggested execution order

```
3.7  sensor access path        <- unblocks 5.4, 5.5, 6.2-6.5
3.8  relay GPIO map            <- unblocks 5.1 users, board.c
2.2  tools/analyze.py          <- makes every later step cheaper
6.8  FSMC transport            <- completes the asset path
5.2  UART + 6.6/6.7           <- brings up both radios
9.1  MQTT + 9.2 HTTP           <- cloud round trip
9.3-9.6 Modbus/BACnet         <- building automation
10.3 differential test         <- proves the HVAC logic
8.5  screens                  <- largest UI effort, do after the core
```

Items 1, 2 (partly), 4, 5.1, 6.1, 7.1, 8.1–8.4 and 10.1–10.2 are done.
