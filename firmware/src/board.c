/*
 * board.c - board-level pin and peripheral assignment.
 *
 * See board.h for how each fact was established. The placeholder
 * assignments below are marked and must be corrected from the schematic
 * before the firmware is flashed to hardware; the logical behaviour does
 * not change.
 */

#include "board.h"
#include "stm32f1xx.h"

/* ---- relays ------------------------------------------------------- */
/* [I] PLACEHOLDER - port/pin assignments not recoverable from the
 * firmware. Relays are grouped on GPIOB/C/D pending schematic review. */
const gpio_t board_relay_w1 = { GPIOB, 12 };
const gpio_t board_relay_w2 = { GPIOB, 13 };
const gpio_t board_relay_y1 = { GPIOB, 14 };
const gpio_t board_relay_y2 = { GPIOB, 15 };
const gpio_t board_relay_g  = { GPIOC, 12 };
const gpio_t board_relay_ob = { GPIOC, 13 };

/* ---- serial ports -------------------------------------------------- */
/* [I] PLACEHOLDER - see board.h. */
USART_TypeDef *const board_uart_esp   = UART5;   /* ESP-07S AT command set */
USART_TypeDef *const board_uart_lora  = USART3;  /* LoRa modem AT set */
USART_TypeDef *const board_uart_rs485 = UART4;   /* Modbus RTU 485 */
USART_TypeDef *const board_uart_debug = USART1;  /* console */

/* ---- display ------------------------------------------------------- */
/* [I] PLACEHOLDER. */
SPI_TypeDef *const board_lcd_spi = SPI3;

/* ---- helpers ------------------------------------------------------- */

void board_init(void)
{
    /* Relay outputs idle low (relays de-energised). */
    const gpio_t *relays[] = {
        &board_relay_w1, &board_relay_w2, &board_relay_y1,
        &board_relay_y2, &board_relay_g, &board_relay_ob
    };
    unsigned i;

    for (i = 0; i < sizeof(relays) / sizeof(relays[0]); i++)
    {
        gpio_init(*relays[i], GPIO_MODE_OUT_PP, GPIO_SPEED_2MHZ);
        gpio_write(*relays[i], 0);
    }
}

void board_relay_set(int index, int on)
{
    const gpio_t *relays[] = {
        &board_relay_w1, &board_relay_w2, &board_relay_y1,
        &board_relay_y2, &board_relay_g, &board_relay_ob
    };

    if (index < 0 || index >= (int)(sizeof(relays) / sizeof(relays[0])))
    {
        return;
    }
    gpio_write(*relays[index], on ? 1 : 0);
}
