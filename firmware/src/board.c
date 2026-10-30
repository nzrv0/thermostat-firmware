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
/* [F] Recovered from the point-registry relay handlers (0x08042674,
 * 0x080426A8, 0x08013268, 0x0801329C, 0x0803EA9C) and confirmed by the
 * output init at 0x0807DC5C, which configures exactly these five pins
 * and drives them high at start-up. */
const gpio_t board_relay_w1 = { GPIOA, 3 };
const gpio_t board_relay_w2 = { GPIOA, 1 };
const gpio_t board_relay_y1 = { GPIOB, 13 };
const gpio_t board_relay_y2 = { GPIOA, 2 };
const gpio_t board_relay_g  = { GPIOB, 14 };
const gpio_t board_relay_ob = { GPIOC, 0 };   /* [I] still unidentified */

/* ---- sensor buses --------------------------------------------------- */
/* [F] Software-bit-banged I2C to the AT24C256 EEPROM: PG6 = SDA,
 * PG8 = SCL (driver at 0x08042BD8, pins configured at 0x08008028). */
const gpio_t board_i2c_sda = { GPIOG, 6 };
const gpio_t board_i2c_scl = { GPIOG, 8 };

/* [F] Two-wire serial bus to the external measurement device:
 * PC8 = data, PC9 = clock (primitives at 0x08061958 / 0x08061B1C). */
const gpio_t board_sensor_data = { GPIOC, 8 };
const gpio_t board_sensor_clock = { GPIOC, 9 };

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

/* The relay index order used by board_relay_set():
 *   0 W1, 1 W2, 2 Y1, 3 Y2, 4 Fan, 5 O/B (pin still unidentified) */
#define BOARD_RELAY_COUNT 5

void board_init(void)
{
    /* [F] The original configures the five known relay pins as
     * push-pull outputs (0x0807DC5C) and drives every one of them high
     * through the BSRR at start-up. The outputs are therefore treated as
     * active-high here as well. The O/B valve pin is not known yet and
     * is deliberately left alone. */
    const gpio_t *relays[] = {
        &board_relay_w1, &board_relay_w2, &board_relay_y1,
        &board_relay_y2, &board_relay_g
    };
    unsigned i;

    for (i = 0; i < sizeof(relays) / sizeof(relays[0]); i++)
    {
        gpio_init(*relays[i], GPIO_MODE_OUT_PP, GPIO_SPEED_2MHZ);
        gpio_write(*relays[i], 1);
    }
}

void board_relay_set(int index, int on)
{
    const gpio_t *relays[] = {
        &board_relay_w1, &board_relay_w2, &board_relay_y1,
        &board_relay_y2, &board_relay_g
    };

    if (index < 0 || index >= (int)(sizeof(relays) / sizeof(relays[0])))
    {
        return;
    }
    gpio_write(*relays[index], on ? 1 : 0);
}
