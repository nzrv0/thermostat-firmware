/*
 * uart.c - byte oriented USART/UART driver.
 */

#include <string.h>
#include "uart.h"

#define USART_SR_RXNE    (1UL << 5)
#define USART_SR_TC      (1UL << 6)
#define USART_SR_TXE     (1UL << 7)
#define USART_CR1_RE     (1UL << 2)
#define USART_CR1_TE     (1UL << 3)
#define USART_CR1_UE     (1UL << 13)
#define USART_CR1_RXNEIE (1UL << 5)

int uart_init(uart_t *uart, USART_TypeDef *port, uint32_t baud,
              uint32_t peripheral_clock)
{
    uint32_t divider;

    if (uart == 0 || port == 0 || baud == 0)
    {
        return -1;
    }

    memset(uart, 0, sizeof(*uart));
    uart->port = port;
    uart->baud = baud;

    port->CR1 = 0;
    divider = peripheral_clock / baud;
    port->BRR = divider & 0xFFFF;
    port->CR2 = 0;
    port->CR3 = 0;
    port->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    return 0;
}

int uart_putc(uart_t *uart, char c)
{
    while ((uart->port->SR & USART_SR_TXE) == 0)
    {
    }
    uart->port->DR = (uint8_t)c;
    uart->tx_count++;
    return 0;
}

int uart_write(uart_t *uart, const uint8_t *data, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++)
    {
        uart_putc(uart, (char)data[i]);
    }
    return (int)length;
}

void uart_irq(uart_t *uart)
{
    uint16_t next;

    if ((uart->port->SR & USART_SR_RXNE) == 0)
    {
        return;
    }

    next = (uint16_t)((uart->rx_head + 1) % UART_RX_BUFFER_SIZE);
    if (next == uart->rx_tail)
    {
        uart->rx_overflow++;
        (void)uart->port->DR;
        return;
    }

    uart->rx[uart->rx_head] = (uint8_t)uart->port->DR;
    uart->rx_head = next;
}

int uart_getc(uart_t *uart, uint8_t *c)
{
    if (uart->rx_tail == uart->rx_head)
    {
        return 0;
    }
    *c = uart->rx[uart->rx_tail];
    uart->rx_tail = (uint16_t)((uart->rx_tail + 1) % UART_RX_BUFFER_SIZE);
    return 1;
}

uint32_t uart_available(const uart_t *uart)
{
    return (uint32_t)((uart->rx_head - uart->rx_tail) % UART_RX_BUFFER_SIZE);
}
