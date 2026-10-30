/*
 * spi.c - SPI master driver for the display bus.
 */

#include "spi.h"

#define SPI_CR1_CPHA   (1UL << 0)
#define SPI_CR1_CPOL   (1UL << 1)
#define SPI_CR1_MSTR   (1UL << 2)
#define SPI_CR1_BR2    (1UL << 3)
#define SPI_CR1_BR1    (1UL << 4)
#define SPI_CR1_BR0    (1UL << 5)
#define SPI_CR1_SPE    (1UL << 6)
#define SPI_CR1_SSI    (1UL << 8)
#define SPI_CR1_DFF    (1UL << 11)
#define SPI_CR1_LSBFIRST (1UL << 7)
#define SPI_SR_RXNE    (1UL << 0)
#define SPI_SR_TXE     (1UL << 1)
#define SPI_SR_BSY     (1UL << 7)

int spi_init(spi_t *spi, SPI_TypeDef *port, uint32_t peripheral_clock,
             uint32_t baud, uint8_t frame_bits)
{
    uint32_t divider;

    if (spi == 0 || port == 0 || baud == 0)
    {
        return -1;
    }
    if (frame_bits != 8 && frame_bits != 16)
    {
        return -1;
    }

    spi->port = port;
    spi->peripheral_clock = peripheral_clock;
    spi->frame_bits = frame_bits;

    divider = peripheral_clock / baud;
    if (divider < 2)
    {
        divider = 2;
    }
    spi->baud_divider = divider;

    port->CR1 = 0;
    port->CR2 = 0;
    port->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SPE;
    if (frame_bits == 16)
    {
        port->CR1 |= SPI_CR1_DFF;
    }
    port->CR1 |= ((divider >> 0) & 7UL) << 3;
    return 0;
}

uint16_t spi_transfer(spi_t *spi, uint16_t value)
{
    uint16_t received = 0;

    while ((spi->port->SR & SPI_SR_TXE) == 0)
    {
    }

    if (spi->frame_bits == 16)
    {
        spi->port->DR = value;
        while ((spi->port->SR & SPI_SR_RXNE) == 0)
        {
        }
        received = (uint16_t)spi->port->DR;
    }
    else
    {
        spi->port->DR = (uint8_t)value;
        while ((spi->port->SR & SPI_SR_RXNE) == 0)
        {
        }
        received = (uint8_t)spi->port->DR;
    }

    while ((spi->port->SR & SPI_SR_BSY) != 0)
    {
    }
    return received;
}
