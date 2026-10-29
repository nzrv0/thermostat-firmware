/*
 * gpio.h - bare GPIO access.
 *
 * The shipped image touches GPIOA..GPIOG heavily (literal-pool counts:
 * A 190, B 150, C 47, G 33, F 9, D 5, E 1), so the pin map matters.
 *
 * The relay terminals are fixed by the board silkscreen (see the PCB
 * back-side photo) and cross-checked against the HVAC terminal-definition
 * strings in the firmware:
 *
 *   W   Heating stage 1            "Heating--W_OB"      0x080741E7
 *   W2  Aux heat stage 2           "Aux Heating--W2_AUX" 0x08073E1C
 *   Y   Cooling stage 1            "1st compressor--Y"  0x08073E78
 *   Y2  Cooling stage 2            "2nd compressor--Y2" 0x08073E64
 *   G   Fan                        "Fan--G"             0x08073DF4
 *   O/B Reversing valve            "O/B Valve--W_OB"    0x08073E50
 *
 * Which GPIO port drives which terminal is not recorded in the firmware
 * itself, so the mapping below is a named placeholder: the driver layer
 * exposes gpio_relay_*() and the port/pin assignment lives in one place
 * (board.c) so it can be corrected from the schematic without touching
 * call sites. See docs/hw-map.md for the open items.
 */

#ifndef GPIO_H
#define GPIO_H

#include "stm32f1xx.h"

typedef enum
{
    GPIO_MODE_IN_ANALOG,
    GPIO_MODE_IN_FLOAT,
    GPIO_MODE_IN_PULLUP,
    GPIO_MODE_IN_PULLDOWN,
    GPIO_MODE_OUT_PP,
    GPIO_MODE_OUT_OD,
    GPIO_MODE_AF_PP,
    GPIO_MODE_AF_OD
} gpio_mode_t;

typedef enum
{
    GPIO_SPEED_10MHZ,
    GPIO_SPEED_2MHZ,
    GPIO_SPEED_50MHZ
} gpio_speed_t;

/* Pin indices are port-relative: port * 16 + pin. */
typedef struct
{
    GPIO_TypeDef *port;
    uint8_t        pin;
} gpio_t;

void gpio_init(gpio_t pin, gpio_mode_t mode, gpio_speed_t speed);
void gpio_write(gpio_t pin, int level);
int  gpio_read(gpio_t pin);
void gpio_toggle(gpio_t pin);

/* Convenience: an 8/16-bit port mask write (BSRR style). */
void gpio_write_mask(GPIO_TypeDef *port, uint16_t mask, int level);

#endif /* GPIO_H */
