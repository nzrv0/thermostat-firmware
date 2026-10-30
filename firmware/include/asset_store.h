/*
 * asset_store.h - FAT32 image store on the external NOR flash.
 *
 * The shipped firmware keeps the UI bitmaps on an SST39VF1601 parallel
 * NOR chip reached through the FSMC at 0x60000000, formatted FAT32. The
 * firmware parses the boot sector itself - it contains the literal
 * "MSDOS5.0", the volume label "NO NAME    " and the filesystem type
 * "FAT32   ", plus a volume label "AT32" at 0x080703DC. That is the
 * classic FatFs f_getlabel / f_mount style probe.
 *
 * The firmware refers to assets by path with a leading backslash and a
 * backslash separator, for example:
 *
 *   \White\105.bmp   \Gray\10.bmp   \fan\20.bmp   \nfan\1.bmp
 *   \Heatbar\15.bmp  logo.bmp       bg.bmp        holisel.bmp
 *
 * 191 path-style references and 295 root-style references were recovered
 * from the image, 486 in total.
 *
 * The supplied asset set (Mark-icons-2025.10.29) covers 419 of them.
 * The shortfall is recorded in docs/assets.md and reported by
 * asset_store_missing() at build time.
 *
 * This module implements the read side: FAT32 directory traversal and
 * file reads, operating on a caller-supplied flash read callback so the
 * same code works against the real FSMC NOR and against an in-memory
 * image in the host test harness.
 */

#ifndef ASSET_STORE_H
#define ASSET_STORE_H

#include <stdint.h>

#define ASSET_PATH_MAX      64
#define ASSET_NAME_MAX      16
#define ASSET_INVALID       0xFFFFFFFFUL

/* Read callback: returns 0 on success and fills *out with len bytes.
 * addr is a byte offset within the flash device. */
typedef int (*asset_read_fn)(void *ctx, uint32_t addr, uint8_t *out, uint32_t len);

typedef struct
{
    asset_read_fn read;
    void         *ctx;

    /* FAT32 layout, filled in by asset_store_mount(). */
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t num_fats;
    uint32_t fat_size_sectors;
    uint32_t root_cluster;
    uint32_t total_sectors;
    uint32_t first_data_sector;
    uint32_t first_root_sector;

    uint8_t  volume_label[12];
} asset_store_t;

/* Mount the store: read and validate the FAT32 boot sector. */
int asset_store_mount(asset_store_t *st, asset_read_fn read, void *ctx);

/* Look up a file by its full path (with or without a leading backslash).
 * Returns the file's first cluster, or ASSET_INVALID. */
uint32_t asset_store_find(const asset_store_t *st, const char *path);

/* Look up a file and return its size. */
uint32_t asset_store_size(const asset_store_t *st, const char *path);

/* Look up a file and return its first cluster and/or its size in one
 * directory traversal. Either output pointer may be NULL. Returns 0 on
 * success, -1 if the path does not resolve. */
int asset_store_lookup(const asset_store_t *st, const char *path,
                       uint32_t *out_cluster, uint32_t *out_size);

/* Read len bytes of a file starting at offset. `first_cluster` is the
 * value returned by asset_store_find(). */
int asset_store_read(const asset_store_t *st, uint32_t first_cluster,
                     uint32_t offset, uint8_t *out, uint32_t len);

#endif /* ASSET_STORE_H */
