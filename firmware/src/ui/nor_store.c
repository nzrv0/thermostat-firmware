/*
 * nor_store.c - the asset store bound to the real NOR flash.
 */

#include "nor_store.h"

int nor_read(void *ctx, uint32_t addr, uint8_t *out, uint32_t len)
{
    (void)ctx;
    fsmc_read_block(addr, out, len);
    return 0;
}

int nor_store_mount(asset_store_t *store)
{
    if (store == 0)
    {
        return -1;
    }
    fsmc_init();
    return asset_store_mount(store, nor_read, 0);
}
