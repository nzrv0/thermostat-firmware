# Toolchain, build system and analysis tooling

## ARM toolchain

**Location:** `toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/`

Installed from the official Arm GNU Toolchain tarball (13.3.Rel1, 147 MB),
unpacked in-tree — **no root access required**. The Ubuntu package
`gcc-arm-none-eabi` is also available via apt, but installing it needs
interactive `sudo`.

```
arm-none-eabi-gcc      13.3.1 20240614
arm-none-eabi-objdump  2.42.0
newlib (nano.specs), libnosys
```

Set `TOOLCHAIN` in `firmware/Makefile` if the path differs:

```bash
make TOOLCHAIN=/path/to/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi
```

## Firmware build

```
cd firmware
make            # build/falcon-app.{elf,bin,hex} + size report
make size       # size only
make clean
```

Key flags: `-mcpu=cortex-m3 -mthumb -mfloat-abi=soft`, `--specs=nano.specs
--specs=nosys.specs`, `-ffunction-sections -fdata-sections` +
`-Wl,--gc-sections`, `-Og -g3` with `-Wall -Wextra -Wshadow -Wundef
-Wdouble-promotion`.

Sources are auto-discovered from `src/{startup,hal,drivers,app,ui,net}`.

### Verified layout match

The build reproduces the shipped image's memory layout exactly:

```
$ arm-none-eabi-readelf -S build/falcon-app.elf | grep -E 'isr_vector|\.text'
  [ 1] .isr_vector  PROGBITS  08005800 000800 000150 00  A  0  0  4
  [ 2] .text        PROGBITS  08005950 000950 000414 00 AX 0  0  4
```

Compare with the original: vector table at `0x08005800`, 336 bytes
(0x150), code starting at `0x08005950`. **Identical.**

## Host test harness

The hardware-independent logic is compiled with the host compiler and
exercised without hardware:

```
cd firmware/tests
make host-test              # builds and runs
make host-test NOR_IMAGE=../build/nor-image.bin
```

Currently **184 checks, 0 failures**, covering:

| Area | Checks |
|---|---|
| HVAC | heat/cool demand, satisfied-state, hold/holiday/occupancy gating, stage-2 soak, heat/cool exclusivity, mode and equipment name strings |
| Framebuffer | fill, rect clipping at edges, 50 % alpha blend, negative-coordinate clipping |
| BMP | valid header parse, dimensions, bit depth, alpha presence, row stride, bad signature rejection, truncated file rejection |
| Asset store | mount, volume label `AT32`, BPB geometry, `\White\105.bmp`, root `logo.bmp`, `\fan\10.bmp`, missing-asset negative case, full BMP header read-back with dimension and bit-depth checks |
| Board map | the recovered relay and sensor-bus pin assignments (W1 = PA3, W2 = PA1, Y1 = PB13, Y2 = PA2, Fan = PB14, I²C on PG6/PG8, measurement bus on PC8/PC9, ADC at `0x40013C00`) |
| EEPROM | the AT24C256 driver driven over the real bit-banged protocol against a simulated device: the `"#E"` signature, a four byte read, the 32 KiB range check and the rejected empty/oversized requests |
| Sensors | the recovered divider maths (`raw × 12500 / 65536 − 600`, clamped 100…9900 Ω) at two exact points, and the ends of the temperature curve |
| Measurement bus | the two-wire bus on PC8/PC9 against a simulated device: six sample bytes, the two command bytes, and the START/STOP framing |
| AT clients | the line oriented client (payload lines, OK/ERROR, timeout) and the Espressif and Dragino command sets and parsers |
| Cloud | the HTTP request shape and form body, and the MQTT-SN CONNECT/PUBLISH/SUBSCRIBE packets |
| Modbus | the register map, the TCP and RTU answers, a write that updates the map, and the exception answers |
| Schedules, metering, faults | the 1440 minute day model and the period in force, the power and running time, the five published faults |
| Scheduler | the tick, the message queue order, the handler dispatch and the overflow counter |

The harness is deliberately the *only* place where behaviour is asserted,
so it stays the regression net for the rest of the rebuild.

## Layout smoke test

`tools/smoke_test.py` compares the rebuilt image against the shipped one
and runs on every `make`:

```bash
cd firmware && make            # builds, sizes, then runs the smoke test
cd firmware && make smoke-test # just the smoke test
```

It checks the vector table at `0x08005800` (336 bytes), code immediately
after it at `0x08005950`, the initial stack pointer word against the
original, every installed vector pointing inside the image, the HEX entry
record, and that nothing lands in the shared parameter block.

## Asset image build

```
python3 tools/mkassets.py --icons Mark-icons-2025.10.29 \
        --firmware Falcon-HP-144pin-8M-Mark-261001.hex \
        --out build/nor-image.bin --manifest build/asset-manifest.txt
```

Produces a 2 MiB FAT32 image (`--format rgb565`, the default) and reports
which of the 486 firmware-referenced assets are missing. See
[assets.md](assets.md).

## Static-analysis tooling

### REA + Hopper (working)

REA 6.7.0 is installed with **Hopper 6.6.0** at `/opt/hopper/bin/Hopper`.
Ghidra is *not* configured and there is no `java`, so REA's raw-binary
import path (which requires Ghidra) is unavailable. Hopper cannot open raw
binaries either.

The working route is to **wrap the flat image in an ELF container** and
let Hopper analyse it:

```bash
python3 tools/hex2elf.py Falcon-HP-144pin-8M-Mark-261001.hex \
        --base 0x08005800 --entry 0x08005950 -o /tmp/app.elf

npx rea-agents@6.7.0 analyze /tmp/app.elf
# -> {"procedure_count": 685, "segment_count": 1}

npx rea-agents@6.7.0 decompile /tmp/app.elf 0x08010338
# -> clean pseudocode, e.g. an STM32F1 GPIO init loop
```

Useful REA commands on this target:

| Command | Result |
|---|---|
| `analyze` | 685 procedures, segment inventory |
| `decompile <addr>` | C-like pseudocode |
| `function <addr>` | full dossier: pseudocode, assembly, callers, callees, xrefs |
| `xrefs <addr>` | incoming references |
| `instructions <addr>` | raw disassembly |
| `search <pattern>` | procedure-name / string search |

**Known REA limitation on this target:** `list_strings` returns empty and
`search_strings` finds nothing, because Hopper does not create string
objects for data that lies inside the executable section — and in this
firmware the string constants are interleaved with code. String discovery
must therefore be done with the standalone scanner below.

### Capstone-based analyser (working)

`tools/analyze.py` provides what REA/Hopper cannot on this target:

- literal-pool resolution per function
- peripheral-base attribution by nearest base with a 4 KB window (the
  technique behind the census in [hw-map.md](hw-map.md))
- string extraction, including the PC-relative (`ADR`) references the
  firmware uses for its embedded constants
- call-graph reconstruction from `BL`/`BLX`, with correct handling of
  conditional branches and of function-pointer roots
- per-function RAM/peripheral usage, string cross-references and
  store-target tracking (`writers`)

It needs `capstone`; the scratch environment lives outside the repository:

```bash
python3 -m venv /tmp/opencode/.venv
/tmp/opencode/.venv/bin/pip install capstone==5.0.7
```

The ARM `objdump` from the in-tree toolchain is the fallback for plain
disassembly:

```bash
toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-objdump \
    -D -b binary -m arm -M force-thumb --adjust-vma=0x08005800 app.bin
```

Usage:

```bash
tools/analyze.py --hex Falcon-HP-144pin-8M-Mark-261001.hex census
tools/analyze.py --hex ... strings --grep "EEPROM"
tools/analyze.py --hex ... func 0x08042674
tools/analyze.py --hex ... range 0x08042900 0x08042d80
tools/analyze.py --hex ... refs 0x20001469
tools/analyze.py --hex ... writers 0x20002248
```

The tool needs the `capstone` module in the interpreter that runs it:
`/tmp/opencode/.venv/bin/python tools/analyze.py …`.

### Python environment

The analysis scratch environment lives in `/tmp/opencode/.venv` with
**capstone 5.0.7**. Not required for the firmware build.


## REA MCP registration (optional)

The registration is now aligned: `~/.config/opencode/opencode.jsonc`
carries `npx -y rea-agents@6.7.0 mcp` with
`HOPPER_LAUNCHER_PATH=/opt/hopper/bin/Hopper`, and
`rea doctor --client opencode --json` reports `healthy`. The previous
file is kept at `~/.config/opencode/opencode.jsonc.rea.backup`.

The REA MCP tools appear in an opencode session started **after** that
change; the CLI works regardless, which is how the analysis in this
repository is run.

```bash
npx -y rea-agents@6.7.0 doctor --client opencode --json
```


## Housekeeping notes

- `tools/mkassets.py` output must not be piped to `head`: the tool dies on
  SIGPIPE mid-write and leaves a truncated image. Redirect to a file
  instead.
- `firmware/build/` and `firmware/tests/build-host/` are disposable.
- `build/` at the repository root holds the generated NOR image and the
  asset manifest; both are regenerated by `tools/mkassets.py`.
