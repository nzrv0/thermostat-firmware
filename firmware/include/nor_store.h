/*
 * nor_store.h - the asset store bound to the real NOR flash.
 *
 * The FAT32 reader in ui/asset_store.c works against an abstract read
 * callback; this module binds it to the FSMC window so the application
 * can read the UI bitmaps from the on-board SST39VF1601.
 */

#ifndef NOR_STORE_H
#define NOR_STORE_H

#include "asset_store.h"
#include "fsmc.h"

/* Read callback over the FSMC window. */
int nor_read(void *ctx, uint32_t addr, uint8_t *out, uint32_t len);

/* Mounts the store from the NOR: initialises the external bus and
 * reads the boot sector. */
int nor_store_mount(asset_store_t *store);

#endif /* NOR_STORE_H */
