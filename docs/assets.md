# UI assets — icons, BMP formats and the NOR image

The UI bitmaps live on an external SST39VF1601 parallel NOR flash as a
FAT32 filesystem. This document records the format details, the tooling,
and the discrepancy between the shipped firmware's expectations and the
supplied icon set.

## The icon set

`Mark-icons-2025.10.29/` — **419 files** (416 BMP + 3 GIF), 3.5 MB total.

```
Mark-icons-2025.10.29/
├── *.bmp          286 root-level icons
├── fan/          20  (fan speed 1..20)
├── nfan/         20  (no-fan variants)
├── Gray/         30  (grayscale scale variants)
├── White/        60  (white/light variants)
└── hun.gif, temc.gif, temf.gif
```

Root icons include `bg.bmp` (480×320 background), `logo.bmp` (200×100),
`holisel.bmp`, `pair1/2.bmp`, `power.bmp`, `detail.bmp`, `warning.bmp`,
`standby.bmp`, `history.bmp`, `digitbg.bmp`, the digit-font set
(`setnum0..9`, `mtnum0..9`, `mtlnum0..9`, `shnum0..9`, `stnum0..9`,
`tmnum0..9`, `wi0..4`) and the UI chrome (`state_bg`, `bg_info_box`,
`ontbl`/`offtbl`/`atbl`/`ctbl`/`htbl`, `eheat`/`ecool`/`eoff`/`doff`/
`dheat`/`dcool`, `esetbtn`/`dsetbtn`, `eup`/`dup`/`edown`/`ddown`).

## BMP formats in the set

| Format | Count | Layout |
|---|---|---|
| 32 bpp BI_RGB | 404 | `B,G,R,A` bytes, uncompressed, **bottom-up** rows, 4-byte aligned stride (already a multiple of 4) |
| 24 bpp BI_RGB | 12 | `B,G,R` bytes, uncompressed, bottom-up, rows padded to 4-byte boundary, no alpha |

The **alpha byte is a real opacity channel**, not a binary mask: values of
`0x00` (transparent), `0xFF` (opaque) and everything in between appear —
the intermediate values are anti-aliased edges. The `White/` and `Gray/`
directories are entirely greyscale (`R == G == B` for every pixel), whereas
`fan/` and `nfan/` are fully coloured.

The decoder in `firmware/src/ui/bmp.c` handles both formats and treats 24bpp
as fully opaque.

## What the firmware expects

486 distinct bitmap references were recovered from the shipped image
(191 path-style with backslashes, 295 root-level):

| Directory | Count | Examples |
|---|---|---|
| `\White\` | 60 | `\White\105.bmp`, `\White\345.bmp` |
| `\Gray\` | 60 | `\Gray\10.bmp`, `\Gray\345.bmp` |
| `\fan\` | 20 | `\fan\1.bmp` … `\fan\20.bmp` |
| `\nfan\` | 20 | `\nfan\1.bmp` … `\nfan\20.bmp` |
| `\Heatbar\` | 31 | `\Heatbar\15.bmp` … `\Heatbar\295.bmp` |
| root | 295 | `logo.bmp`, `bg.bmp`, `holisel.bmp`, … |

## Discrepancy: the supplied set is a different revision

The input icon folder is dated **2025-10-29**; the firmware is **Mark
261001** (2026-10-01). They do not fully correspond:

- **100 of the 486 referenced assets are absent** from the supplied folder:
  - `Gray\` — 30 missing (the `55`, `65`, … `345` variants; the folder only
    has `6`–`35`)
  - `Heatbar\` — **all 31 missing** (the folder has no `Heatbar` directory)
  - root — 39 missing (`shnum0..9`, `stnum0..9`, `tmnum0..9`, `wi0/1/3/4`,
    `bdot`, `digitbg3`, `rfset`, `wifilock`, and the concatenation artefact
    `fanset.bmpfloor.bmp`)
- **33 files are present but unreferenced** by this firmware build
  (`accu.bmp`, `fan_off.bmp`, `faschumi.bmp`, `fcool1/2.bmp`, `fheat1/2.bmp`,
  `ftmon/ftmoff.bmp`, `opton/optoff.bmp`, `simon.bmp`, `tcmode.bmp`,
  `tfmode.bmp`, `wifi1/2/4.bmp`, `wifi_r4_c4.bmp`, `levelrow.bmp`,
  `unaccu.bmp`, `tlimit.bmp`, the three GIFs, …) — these are presumably
  used by other product variants (MC6_E / MC6_2FCU / MC6_4FCU).

`tools/mkassets.py` reports this on every run and writes a machine-readable
manifest:

```
$ python3 tools/mkassets.py --icons Mark-icons-2025.10.29 \
      --firmware Falcon-HP-144pin-8M-Mark-261001.hex \
      --out build/nor-image.bin --manifest build/asset-manifest.txt
firmware references 486 assets; 386 present, 100 missing
```

`build/asset-manifest.txt` lists every referenced asset with `ok` or
`MISSING`.

## Storage format decision

The NOR is a **16 Mbit (2 MiB)** part. The asset set as 32bpp BGRA is
**3.5 MiB** — it does not fit. As 16bpp RGB565 it is **1.75 MiB** and does
fit, with room for the filesystem.

That is consistent with the display: a 480×320 RGB565 framebuffer is
300 KiB, far beyond the 96 KiB SRAM, so the panel must carry its own GRAM
and the assets are stored pre-converted to the panel's native format.

`tools/mkassets.py --format rgb565` (the default) therefore converts each
BMP to a 16bpp BI_RGB BMP and writes it into the FAT32 image. The
`--format bgra32` option keeps the original 32bpp for inspection, but
**cannot fit on the device**.

### Cluster size

FAT32 defaults to 1 KiB or larger clusters. With 416 files the per-file
rounding waste alone pushes the image past the 2 MiB capacity, so the
packer uses **1 sector (512 B) per cluster**.

## FAT32 image layout produced

```
0x0000  boot sector   "MSDOS5.0", "FAT32", volume label "AT32"
0x0020  FAT #1        31 sectors
0x0040  FAT #2        31 sectors
0x0060  data area     cluster 2 = root directory, cluster 3+ = file data
```

Verified by an independent Python FAT32 parser and by the C
`asset_store.c` implementation:

- 392 root-directory entries (volume label, `.`, `..`, 4 subdirectories,
  286 root files)
- Subdirectories `GRAY`, `WHITE`, `FAN`, `NFAN` resolve, each with the
  expected entries and correct BMP headers
- `logo.bmp` reads back as a valid 200×100 16bpp BMP

## The C side

`firmware/src/ui/asset_store.c` — FAT32 reader:

```c
asset_store_t st;
asset_store_mount(&st, read_cb, ctx);       /* parses the boot sector   */
uint32_t c = asset_store_find(&st, "\\White\\105.bmp");
uint32_t n = asset_store_size(&st, "\\White\\105.bmp");
asset_store_read(&st, c, 0, buf, n);
```

The read callback abstracts the transport, so the same code runs against
the real FSMC NOR and against an in-memory image in the host harness.

Path conventions follow the firmware exactly: a leading backslash is
optional, separators are backslashes, and matching is **case-insensitive**
(the firmware says `\White` while FAT stores `WHITE`).

`firmware/src/ui/framebuffer.c` — RGB565 framebuffer with fill, rect,
pixel and alpha-blended blit, plus clipping.

`firmware/src/ui/bmp.c` — BMP header parsing and drawing, handling
bottom-up row order, 32bpp with alpha and 24bpp.

## Known limitations

- The rebuild does not yet read assets **from the device's NOR**; it reads
  them from the generated image. The FSMC driver and the NOR read path are
  still to be written (TODO 6.4).
- `\Heatbar\` assets are entirely absent, so that UI element cannot be
  reproduced from the supplied material. Either the original NOR image is
  needed, or the element must be recreated.
- The three GIFs are unused by this firmware build and are not packed.
