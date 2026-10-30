/*
 * spi.h - SPI master driver for the display bus.
 *
 * SPI1 (0x40013400) and SPI3 (0x40003C00) are the two SPI blocks the
 * firmware uses; the panel is on one of them (open question #7).
 */

#ifndef SPI_H
#define SPI_H

#include <stdint.h>
#include "stm32f1xx.h"

typedef struct
{
    SPI_TypeDef *port;
    uint32_t     peripheral_clock;
    uint32_t     baud_divider;
    uint8_t      frame_bits;   /* 8 or 16 */
} spi_t;

int spi_init(spi_t *spi, SPI_TypeDef *port, uint32_t peripheral_clock,
             uint32_t baud, uint8_t frame_bits);

/* Transfers one frame and returns the value shifted in. */
uint16_t spi_transfer(spi_t *spi, uint16_t value);

#endif /* SPI_H */
