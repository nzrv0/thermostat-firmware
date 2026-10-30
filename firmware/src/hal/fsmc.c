/*
 * fsmc.c - external NOR flash on the FSMC.
 *
 * The bus registers live at 0xA0000000; the original uses 0xA0000104 and
 * 0xA0000220 (docs/hw-map.md).
 */

#include "fsmc.h"
#include "stm32f1xx.h"

void fsmc_init(void)
{
    RCC->AHBENR |= RCC_AHBENR_FSMCEN;

    FSMC_Bank1->PCR1 = (1UL << 1)     /* PBANK */
                    | (1UL << 4)     /* MEMORY_DATA_WIDTH = 8 bit */
                    | (1UL << 6)     /* MEMORY_MODE = asynchronous */
                    | (1UL << 9);    /* MEMORY_BUS_WIDTH = 8 bit */
    FSMC_Bank1->PMEM1 = 0;
    FSMC_Bank1->PER1 = 0;
}

uint8_t fsmc_read_byte(uint32_t offset)
{
    const volatile uint8_t *window =
        (const volatile uint8_t *)(FSMC_NOR_BASE + (offset & (FSMC_NOR_SIZE_BYTES - 1)));

    return *window;
}

void fsmc_read_block(uint32_t offset, void *buffer, uint32_t length)
{
    uint8_t *out = (uint8_t *)buffer;
    uint32_t i;

    for (i = 0; i < length; i++)
    {
        out[i] = fsmc_read_byte(offset + i);
    }
}

uint16_t fsmc_read_id(uint32_t offset)
{
    const volatile uint16_t *window =
        (const volatile uint16_t *)(FSMC_NOR_BASE + FSMC_NOR_ID_OFFSET + offset);

    return *window;
}
