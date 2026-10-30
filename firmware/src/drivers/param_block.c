/*
 * param_block.c - read/write the shared parameter block in flash.
 */

#include "param_block.h"
#include "stm32f1xx.h"

#define FLASH_PAGE_SIZE 2048UL

static int flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK)
    {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
    return (FLASH->CR & FLASH_CR_LOCK) ? -1 : 0;
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

/* Erase the 2 KiB page containing addr. */
static int flash_erase_page(uint32_t addr)
{
    uint32_t timeout;

    if (flash_unlock() != 0)
    {
        return -1;
    }

    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR  = addr;
    FLASH->CR |= FLASH_CR_STRT;

    for (timeout = 0; timeout < 0x100000UL; timeout++)
    {
        if (FLASH->SR & FLASH_SR_BSY)
        {
            continue;
        }
        break;
    }

    FLASH->CR &= ~FLASH_CR_PER;

    if (FLASH->SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR))
    {
        FLASH->SR = FLASH_SR_PGERR | FLASH_SR_WRPRTERR;
        flash_lock();
        return -1;
    }

    return 0;
}

/* Program one halfword at addr. addr must be even and already erased. */
static int flash_program_halfword(uint32_t addr, uint16_t value)
{
    uint32_t timeout;

    if (flash_unlock() != 0)
    {
        return -1;
    }

    FLASH->CR |= FLASH_CR_PG;
    *(volatile uint16_t *)addr = value;

    for (timeout = 0; timeout < 0x100000UL; timeout++)
    {
        if (FLASH->SR & FLASH_SR_BSY)
        {
            continue;
        }
        break;
    }

    FLASH->CR &= ~FLASH_CR_PG;

    if (FLASH->SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR))
    {
        FLASH->SR = FLASH_SR_PGERR | FLASH_SR_WRPRTERR;
        flash_lock();
        return -1;
    }

    return 0;
}

int param_block_read(uint8_t *dst, uint32_t max_len)
{
    const uint32_t *src = (const uint32_t *)PARAM_BLOCK_PAYLOAD;
    uint32_t i;

    if (param_block_check() != PARAM_OK)
    {
        return -PARAM_ERR_MAGIC;
    }

    for (i = 0; i < max_len; i++)
    {
        dst[i] = ((const uint8_t *)src)[i];
    }

    return (int)((max_len < PARAM_BLOCK_MAX_SIZE) ? max_len : PARAM_BLOCK_MAX_SIZE);
}

param_status_t param_block_check(void)
{
    const uint32_t *magic = (const uint32_t *)PARAM_BLOCK_ADDR;

    if (*magic != PARAM_BLOCK_MAGIC)
    {
        return PARAM_ERR_MAGIC;
    }
    return PARAM_OK;
}

param_status_t param_block_store(const uint8_t *src, uint32_t len)
{
    uint32_t i;
    static uint8_t page[FLASH_PAGE_SIZE];

    if (len > PARAM_BLOCK_MAX_SIZE)
    {
        return PARAM_ERR_SIZE;
    }

    /* The region spans two flash pages when it straddles a page boundary;
     * handle the general case by erasing each page that is touched. */
    for (i = 0; i < len; i++)
    {
        uint32_t addr = PARAM_BLOCK_PAYLOAD + i;
        uint32_t page_base = addr & ~(FLASH_PAGE_SIZE - 1UL);

        if (flash_erase_page(page_base) != 0)
        {
            return PARAM_ERR_FLASH;
        }
    }

    /* Program the magic word first so a partially written block is
     * detectable. */
    if (flash_program_halfword(PARAM_BLOCK_ADDR,
                               (uint16_t)(PARAM_BLOCK_MAGIC & 0xFFFF)) != 0 ||
        flash_program_halfword(PARAM_BLOCK_ADDR + 2,
                               (uint16_t)(PARAM_BLOCK_MAGIC >> 16)) != 0)
    {
        return PARAM_ERR_FLASH;
    }

    for (i = 0; i < len; i += 2)
    {
        uint16_t v;
        uint32_t addr = PARAM_BLOCK_PAYLOAD + i;

        if (i + 1 < len)
        {
            v = (uint16_t)(src[i] | ((uint16_t)src[i + 1] << 8));
        }
        else
        {
            v = src[i];
        }

        if (flash_program_halfword(addr, v) != 0)
        {
            return PARAM_ERR_FLASH;
        }
    }

    flash_lock();
    (void)page;
    return PARAM_OK;
}
