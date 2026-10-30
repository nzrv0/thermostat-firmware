/*
 * uart.h - byte oriented USART/UART driver.
 *
 * The image uses five UARTs: USART1 (the ESP-07S modem, IRQ 37),
 * USART2, USART3, UART4 (IRQ 52) and UART5. The interrupt path is byte
 * oriented, which is what the AT clients above expect.
 */

#ifndef UART_H
#define UART_H

#include <stddef.h>
#include <stdint.h>
#include "stm32f1xx.h"

#define UART_RX_BUFFER_SIZE 256

typedef struct
{
    USART_TypeDef *port;
    uint32_t       baud;
    volatile uint8_t rx[UART_RX_BUFFER_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    volatile uint32_t rx_overflow;
    volatile uint32_t tx_count;
} uart_t;

/* Brings the port up: enables the peripheral clock, sets the baud rate
 * and 8N1, and enables the receive interrupt. */
int uart_init(uart_t *uart, USART_TypeDef *port, uint32_t baud,
               uint32_t peripheral_clock);

/* Blocking send. */
int uart_write(uart_t *uart, const uint8_t *data, size_t length);
int uart_putc(uart_t *uart, char c);

/* Non blocking receive; returns 0 when the buffer is empty. */
int uart_getc(uart_t *uart, uint8_t *c);

/* Called from the interrupt handler. */
void uart_irq(uart_t *uart);

/* The number of bytes waiting. */
uint32_t uart_available(const uart_t *uart);

#endif /* UART_H */
