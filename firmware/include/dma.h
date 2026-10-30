/*
 * dma.h - DMA1 channel driver.
 *
 * The firmware uses DMA1 for display blits and UART transfers. This
 * driver covers the transfer mode the rebuild needs: a single memory to
 * peripheral (or peripheral to memory) block on one channel.
 */

#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include "stm32f1xx.h"

typedef struct
{
    DMA_Channel_TypeDef *channel;
    uint8_t              index;
    uint32_t             peripheral_address;
} dma_t;

/* Prepares a channel for a memory to peripheral transfer. */
int dma_init_mem_to_periph(dma_t *dma, uint8_t channel,
                           uint32_t peripheral_address, uint32_t peripheral_clock);

/* Prepares a channel for a peripheral to memory transfer. */
int dma_init_periph_to_mem(dma_t *dma, uint8_t channel,
                           uint32_t peripheral_address, uint32_t peripheral_clock);

/* Starts a transfer of length bytes. */
int dma_start(dma_t *dma, uint32_t memory_address, uint32_t length);

/* True when the transfer has finished. */
int dma_complete(const dma_t *dma);

#endif /* DMA_H */
