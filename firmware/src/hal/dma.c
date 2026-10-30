/*
 * dma.c - DMA1 channel driver.
 */

#include "dma.h"

static int dma_setup(dma_t *dma, uint8_t channel, uint32_t peripheral_address,
                     uint32_t peripheral_clock, uint32_t direction)
{
    uint32_t divider;

    if (dma == 0 || channel == 0 || channel > 7 || peripheral_clock == 0)
    {
        return -1;
    }

    dma->index = channel;
    dma->channel = &DMA1->CH[channel - 1];
    dma->peripheral_address = peripheral_address;

    divider = peripheral_clock / 1000000u;
    if (divider < 1)
    {
        divider = 1;
    }

    dma->channel->CCR = 0;
    dma->channel->CPAR = peripheral_address;
    dma->channel->CMAR = 0;
    dma->channel->CNDTR = 0;
    dma->channel->CCR = DMA_CCR_MINC | DMA_CCR_TCIE | direction |
                        (((divider - 1u) & 0x7u) << DMA_CCR_PSIZE_Pos) |
                        ((direction & 0x1u) << 4);
    return 0;
}

int dma_init_mem_to_periph(dma_t *dma, uint8_t channel,
                           uint32_t peripheral_address, uint32_t peripheral_clock)
{
    return dma_setup(dma, channel, peripheral_address, peripheral_clock, 0);
}

int dma_init_periph_to_mem(dma_t *dma, uint8_t channel,
                           uint32_t peripheral_address, uint32_t peripheral_clock)
{
    return dma_setup(dma, channel, peripheral_address, peripheral_clock,
                     DMA_CCR_DIR);
}

int dma_start(dma_t *dma, uint32_t memory_address, uint32_t length)
{
    if (dma == 0 || dma->channel == 0 || length == 0)
    {
        return -1;
    }
    dma->channel->CMAR = memory_address;
    dma->channel->CNDTR = length;
    dma->channel->CCR |= DMA_CCR_EN;
    return 0;
}

int dma_complete(const dma_t *dma)
{
    if (dma == 0 || dma->channel == 0)
    {
        return 0;
    }
    return (dma->channel->CNDTR == 0) ? 1 : 0;
}
