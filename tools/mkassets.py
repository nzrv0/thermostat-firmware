#!/usr/bin/env python3
"""Build a FAT32 image for the external NOR flash from the icon set.

The shipped firmware keeps its UI bitmaps in a FAT32 filesystem on an
SST39VF1601 parallel NOR chip reached over FSMC at 0x60000000. It refers
to them by path with backslash separators, e.g. "\\White\\105.bmp".

This tool packs the icons in Mark-icons-2025.10.29 into such an image
and reports which of the 486 assets the firmware references are missing.

Usage:
    tools/mkassets.py --icons Mark-icons-2025.10.29 \
                      --manifest build/asset-manifest.txt \
                      --out build/nor-image.bin
"""

import argparse
import os
import struct
import sys

SECTOR = 512
# 1 sector (512 B) per cluster. The asset set is ~1.8 MiB of pixel data
# and the SST39VF1601 is only 2 MiB, so the cluster size must be small:
# at 4 KiB per cluster the per-file rounding waste alone pushes the image
# over the device's capacity.
CLUSTER = 1          # 512 B per cluster
RESERVED = 32
NUM_FATS = 2
VOLUME_LABEL = "AT32"
ROOT_ENTRIES = 512
NOR_SIZE = 2 * 1024 * 1024


def read_bmp(path):
    """Return (pixels_bgra, width, height) with rows top-down."""
    with open(path, "rb") as fh:
        d = fh.read()
    if d[:2] != b"BM":
        raise ValueError("not a BMP: %s" % path)
    off = struct.unpack("<I", d[10:14])[0]
    w, h = struct.unpack("<ii", d[18:26])
    bpp = struct.unpack("<H", d[28:30])[0]
    if bpp == 32:
        stride = w * 4
    elif bpp == 24:
        stride = (w * 3 + 3) & ~3
    else:
        raise ValueError("unsupported bpp %d in %s" % (bpp, path))

    rows = []
    for y in range(abs(h)):
        src = off + (abs(h) - 1 - y) * stride
        row = d[src:src + stride]
        if bpp == 32:
            rows.append(row[:w * 4])
        else:
            # Expand 24bpp BGR to 32bpp BGRA with opaque alpha.
            out = bytearray()
            for x in range(w):
                b, g, r = row[x * 3:x * 3 + 3]
                out += bytes((b, g, r, 255))
            rows.append(bytes(out))
    return b"".join(rows), w, abs(h)


def to_rgb565_bmp(pixels, w, h):
    """Convert a 32bpp BGRA buffer to a 16bpp BI_RGB BMP (bottom-up).

    The external NOR is a 16 Mbit (2 MiB) SST39VF1601, and the full asset
    set is 3.5 MiB as 32bpp - it does not fit. At 16bpp it is 1.75 MiB and
    does fit, and 16bpp is also the panel's native format (a 480x320
    RGB565 framebuffer is 300 KiB, far larger than the 96 KiB SRAM, so the
    panel carries its own GRAM and assets are stored pre-converted).
    """
    stride = ((w * 2) + 3) & ~3
    rows = []
    for y in range(h):
        row = bytearray()
        src = y * w * 4
        for x in range(w):
            b = pixels[src + x * 4 + 0]
            g = pixels[src + x * 4 + 1]
            r = pixels[src + x * 4 + 2]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            row += struct.pack("<H", v)
        row += b"\x00" * (stride - w * 2)
        rows.append(bytes(row))

    pixels_data = b"".join(reversed(rows))     # BMP stores bottom-up
    dib = bytearray(40)
    struct.pack_into("<I", dib, 0, 40)
    struct.pack_into("<i", dib, 4, w)
    struct.pack_into("<i", dib, 8, h)
    struct.pack_into("<H", dib, 12, 1)
    struct.pack_into("<H", dib, 14, 16)
    struct.pack_into("<I", dib, 16, 0)          # BI_RGB
    struct.pack_into("<I", dib, 20, len(pixels_data))
    struct.pack_into("<I", dib, 24, 2835)
    struct.pack_into("<I", dib, 28, 2835)
    struct.pack_into("<I", dib, 32, 0)
    struct.pack_into("<I", dib, 36, 0)

    off = 14 + 40
    header = b"BM" + struct.pack("<IHHI", off + len(pixels_data), 0, 0, off)
    return bytes(header + dib + pixels_data)


def short_name(name):
    """Return an 8.3 uppercase short name, or None if it does not fit."""
    base, _, ext = name.rpartition(".")
    base = base.upper().replace(" ", "")
    ext = ext.upper().replace(" ", "")
    if not base or len(base) > 8 or len(ext) > 3:
        return None
    return base.ljust(8) + ext.ljust(3)


def build_fat32(files):
    """files: list of (path_in_image, bytes). Returns the image bytes.

    Two-pass layout:
      1. allocate clusters for file data and for each sub-directory,
      2. size the FAT from the total cluster count, then emit.
    """
    # ---- pass 1: cluster allocation -------------------------------------
    # FAT32 reserves cluster 2 for the root directory. The root
    # directory needs room for: the volume label, "." and "..", one
    # entry per sub-directory, and one entry per root-level file.
    cluster_bytes = CLUSTER * SECTOR

    subdirs = {}
    for path, _ in files:
        d = os.path.dirname(path)
        if d:
            subdirs.setdefault(d, []).append(path)

    root_entries = 1 + 2 + len(subdirs) + sum(
        1 for p, _ in files if not os.path.dirname(p))
    root_clusters = max(1, (root_entries * 32 + cluster_bytes - 1) // cluster_bytes)

    # File data starts after the root directory's clusters.
    file_alloc = []          # (path, first_cluster, n_clusters, size)
    next_cluster = 2 + root_clusters

    for path, blob in files:
        n = max(1, (len(blob) + cluster_bytes - 1) // cluster_bytes)
        file_alloc.append((path, next_cluster, n, len(blob)))
        next_cluster += n

    dir_alloc = {}           # dir -> (first_cluster, n_clusters)
    entries_per_cluster = cluster_bytes // 32
    for d in sorted(subdirs):
        # Each 32-byte entry plus "." and "..".
        need = max(1, (len(subdirs[d]) + 2 + entries_per_cluster - 1)
                   // entries_per_cluster)
        dir_alloc[d] = (next_cluster, need)
        next_cluster += need

    last_cluster = next_cluster          # one past the last used cluster

    # ---- pass 2: FAT and geometry --------------------------------------
    fat_entries = last_cluster + 2       # +2 for the reserved FAT entries
    fat_sectors = (fat_entries * 4 + SECTOR - 1) // SECTOR
    # FAT32 keeps no separate root-directory area: the root directory is
    # cluster 2, which lives in the data area like any other cluster.
    total_sectors = RESERVED + NUM_FATS * fat_sectors \
        + (last_cluster - 2) * CLUSTER

    fat = bytearray(fat_sectors * SECTOR)
    fat[0:4] = struct.pack("<I", 0x0FFFFFF8)
    fat[4:8] = struct.pack("<I", 0x0FFFFFFF)

    def chain(first, n):
        for i in range(n - 1):
            off = (first + i) * 4
            fat[off:off + 4] = struct.pack("<I", first + i + 1)
        off = (first + n - 1) * 4
        fat[off:off + 4] = struct.pack("<I", 0x0FFFFFFF)

    for _, first, n, _sz in file_alloc:
        chain(first, n)
    for d, (first, n) in dir_alloc.items():
        chain(first, n)
    # The root directory chain starts at cluster 2.
    chain(2, root_clusters)

    # ---- root directory --------------------------------------------------
    root = bytearray(root_clusters * cluster_bytes)
    entries = 0

    def put(root_buf, e, name_attr, attr, first_cluster, size):
        off = e * 32
        root_buf[off:off + 11] = name_attr.encode("ascii")
        root_buf[off + 11] = attr
        root_buf[off + 20:off + 22] = struct.pack("<H", first_cluster & 0xFFFF)
        root_buf[off + 22:off + 24] = struct.pack("<H", (first_cluster >> 16) & 0xFFFF)
        root_buf[off + 28:off + 32] = struct.pack("<I", size)

    # volume label
    put(root, entries, VOLUME_LABEL.ljust(11), 0x08, 0, 0)
    entries += 1
    # "." and ".."
    put(root, entries, ".          ", 0x10, 2, 0)
    entries += 1
    put(root, entries, "..         ", 0x10, 0, 0)
    entries += 1
    # sub-directory entries
    for d in sorted(dir_alloc):
        first, _n = dir_alloc[d]
        put(root, entries, os.path.basename(d).upper().ljust(11), 0x10, first, 0)
        entries += 1
    # root-level files
    for path, first, _n, size in file_alloc:
        if os.path.dirname(path):
            continue
        sn = short_name(os.path.basename(path))
        if sn is None:
            continue
        put(root, entries, sn, 0x20, first, size)
        entries += 1

    # ---- sub-directory contents -----------------------------------------
    dir_buf = {}
    for d, ents in subdirs.items():
        first, n = dir_alloc[d]
        buf = bytearray(n * cluster_bytes)
        e = 0
        put(buf, e, ".          ", 0x10, first, 0); e += 1
        put(buf, e, "..         ", 0x10, 2, 0); e += 1
        for path in ents:
            fp, fc, _fn, fsz = [(a, b, c, dd) for (a, b, c, dd) in file_alloc
                                 if a == path][0]
            sn = short_name(os.path.basename(path))
            if sn is None:
                continue
            put(buf, e, sn, 0x20, fc, fsz)
            e += 1
        dir_buf[d] = (first, buf)

    # ---- assemble the image ----------------------------------------------
    image = bytearray(NOR_SIZE)

    # boot sector
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xEB\x58\x90"
    bs[3:11] = b"MSDOS5.0"
    struct.pack_into("<H", bs, 0x0B, SECTOR)
    bs[0x0D] = CLUSTER
    struct.pack_into("<H", bs, 0x0E, RESERVED)
    bs[0x10] = NUM_FATS
    struct.pack_into("<H", bs, 0x13, 0)
    struct.pack_into("<H", bs, 0x16, 0)
    struct.pack_into("<H", bs, 0x18, 32)          # sectors per FAT (16)
    struct.pack_into("<I", bs, 0x20, total_sectors)
    struct.pack_into("<H", bs, 0x24, fat_sectors)   # sectors per FAT (32)
    bs[0x2B:0x36] = VOLUME_LABEL.ljust(11).encode("ascii")
    bs[0x36:0x3B] = b"FAT32"
    bs[0x40] = 0x80
    bs[0x42] = 0x29                              # ext boot signature
    bs[0x43] = 0x29
    struct.pack_into("<I", bs, 0x44, 2)          # root cluster
    struct.pack_into("<H", bs, 0x48, 1)          # FS info sector
    struct.pack_into("<H", bs, 0x4A, 6)          # backup boot sector
    bs[0x50] = 0x29
    bs[0x52] = 0x29
    bs[0x1FE] = 0x55
    bs[0x1FF] = 0xAA
    image[0:SECTOR] = bs

    fat_off = RESERVED * SECTOR
    for i in range(NUM_FATS):
        o = fat_off + i * fat_sectors * SECTOR
        image[o:o + len(fat)] = fat

    data_off = (RESERVED + NUM_FATS * fat_sectors) * SECTOR

    # The root directory occupies cluster 2 of the data area.
    root_off = data_off
    image[root_off:root_off + len(root)] = root

    # file data
    blob_by_path = dict(files)
    for path, first, n, size in file_alloc:
        blob = blob_by_path[path]
        o = data_off + (first - 2) * cluster_bytes
        image[o:o + len(blob)] = blob

    # directory data
    for d, (first, buf) in dir_buf.items():
        o = data_off + (first - 2) * cluster_bytes
        image[o:o + len(buf)] = buf

    return bytes(image)


def firmware_manifest(path):
    """Extract the .bmp references from the firmware image."""
    data = {}
    cur = 0
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            b = bytes.fromhex(line[1:])
            n, a, t, p = b[0], (b[1] << 8) | b[2], b[3], b[4:4 + b[0]]
            if t == 0:
                for i, v in enumerate(p):
                    data[(cur << 16) + a + i] = v
            elif t == 4:
                cur = struct.unpack(">H", p)[0]
    lo, hi = min(data), max(data)
    blob = bytes(data.get(a, 0xFF) for a in range(lo, hi + 1))

    import re
    names = set()
    for m in re.finditer(rb"[\x20-\x7e]{2,}\.bmp", blob):
        names.add(m.group(0).decode("latin1").strip().lstrip("\\"))
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--icons", required=True)
    ap.add_argument("--firmware", default=None,
                    help="firmware .hex to cross-check against")
    ap.add_argument("--out", required=True)
    ap.add_argument("--manifest", default=None)
    ap.add_argument("--format", choices=("rgb565", "bgra32"), default="rgb565",
                    help="on-flash pixel format (default rgb565: the NOR is "
                         "2 MiB and the 32bpp set does not fit)")
    args = ap.parse_args()

    files = []
    total = 0
    for root, dirs, fs in os.walk(args.icons):
        dirs.sort()
        for f in sorted(fs):
            if not f.lower().endswith(".bmp"):
                continue
            p = os.path.join(root, f)
            # Internal paths use '/' so os.path.dirname() splits them;
            # they are converted to the FAT backslash form on write.
            rel = os.path.relpath(p, args.icons)
            pixels, w, h = read_bmp(p)
            if args.format == "rgb565":
                pixels = to_rgb565_bmp(pixels, w, h)
            files.append((rel, pixels))
            total += len(pixels)

    print("packed %d bitmaps as %s, %.1f KiB of image data (NOR is 2048 KiB)"
          % (len(files), args.format, total / 1024))
    if total > 2 * 1024 * 1024:
        print("WARNING: image data exceeds the 2 MiB NOR capacity")

    image = build_fat32(files)
    with open(args.out, "wb") as fh:
        fh.write(image)
    print("wrote %s (%d bytes)" % (args.out, len(image)))

    if args.firmware:
        want = firmware_manifest(args.firmware)
        have = {p for p, _ in files}
        missing = sorted(w for w in want if w not in have)
        extra = sorted(h for h in have if h not in want)
        print()
        print("firmware references %d assets; %d present, %d missing" %
              (len(want), len(want) - len(missing), len(missing)))
        if missing:
            print("missing:")
            for m in missing:
                print("   ", m)
        if extra:
            print("present but unreferenced: %d" % len(extra))
        if args.manifest:
            with open(args.manifest, "w") as fh:
                for w in sorted(want):
                    fh.write("%s\t%s\n" % (w, "ok" if w in have else "MISSING"))
            print("wrote %s" % args.manifest)
        return 1 if missing else 0

    return 0


if __name__ == "__main__":
    sys.exit(main())
