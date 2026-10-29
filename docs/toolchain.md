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

Currently **43 checks, 0 failures**, covering:

| Area | Checks |
|---|---|
| HVAC | heat/cool demand, satisfied-state, hold/holiday/occupancy gating, stage-2 soak, heat/cool exclusivity, mode and equipment name strings |
| Framebuffer | fill, rect clipping at edges, 50 % alpha blend, negative-coordinate clipping |
| BMP | valid header parse, dimensions, bit depth, alpha presence, row stride, bad signature rejection, truncated file rejection |
| Asset store | mount, volume label `AT32`, BPB geometry, `\White\105.bmp`, root `logo.bmp`, `\fan\10.bmp`, missing-asset negative case, full BMP header read-back with dimension and bit-depth checks |

The harness is deliberately the *only* place where behaviour is asserted,
so it stays the regression net for the rest of the rebuild.

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

### Capstone-based analyser (planned)

`tools/analyze.py` (TODO 2.2) will provide what REA/Hopper cannot:

- literal-pool resolution per function
- peripheral-base attribution (the technique behind the census in
  [hw-map.md](hw-map.md))
- string extraction with the correct FAT-style path forms
- call-graph reconstruction from `BL` targets
- per-function RAM and peripheral usage

### Python environment

The analysis scratch environment lives in `/tmp/opencode/.venv` with
**capstone 5.0.7**. Not required for the firmware build.

## REA MCP registration (optional)

`rea doctor --client opencode` reports `config_drift` for the opencode
registration: the MCP server is not registered in
`~/.config/opencode/opencode.jsonc`. The CLI works regardless, so this is
only needed if interactive REA tools are wanted in-session:

```bash
npx rea-agents@6.7.0 setup --client opencode --dry-run --json   # review
npx rea-agents@6.7.0 setup --client opencode --yes               # apply
# then restart the client
```

## Housekeeping notes

- `tools/mkassets.py` output must not be piped to `head`: the tool dies on
  SIGPIPE mid-write and leaves a truncated image. Redirect to a file
  instead.
- `firmware/build/` and `firmware/tests/build-host/` are disposable.
- `build/` at the repository root holds the generated NOR image and the
  asset manifest; both are regenerated by `tools/mkassets.py`.
