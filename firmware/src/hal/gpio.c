/*
 * gpio.c - bare GPIO access.
 */

#include "gpio.h"

static void gpio_set_mode(GPIO_TypeDef *port, uint8_t pin,
                          gpio_mode_t mode, gpio_speed_t speed)
{
    volatile uint32_t *reg = (pin < 8) ? &port->CRL : &port->CRH;
    uint32_t shift = (pin & 7) * 4;
    uint32_t cfg;

    switch (mode)
    {
    case GPIO_MODE_IN_ANALOG: cfg = GPIO_CNF_IN_ANALOG; break;
    case GPIO_MODE_IN_FLOAT:  cfg = GPIO_CNF_IN_FLOAT;  break;
    case GPIO_MODE_IN_PULLUP: cfg = GPIO_CNF_IN_PUPD;   break;
    case GPIO_MODE_IN_PULLDOWN: cfg = GPIO_CNF_IN_PUPD; break;
    case GPIO_MODE_OUT_PP:   cfg = GPIO_CNF_OUT_PP;   break;
    case GPIO_MODE_OUT_OD:   cfg = GPIO_CNF_OUT_OD;   break;
    case GPIO_MODE_AF_PP:    cfg = GPIO_CNF_AF_PP;    break;
    case GPIO_MODE_AF_OD:    cfg = GPIO_CNF_AF_OD;    break;
    default:                 cfg = GPIO_CNF_IN_FLOAT; break;
    }

    if (mode >= GPIO_MODE_OUT_PP && mode != GPIO_MODE_IN_PULLDOWN)
    {
        cfg |= (uint32_t)speed;
    }
    else if (mode == GPIO_MODE_IN_PULLUP || mode == GPIO_MODE_IN_PULLDOWN)
    {
        /* F1 has a single pull direction per pin; pull-down is emulated
         * by an external resistor, so both map to the same CNF. */
        cfg |= GPIO_CNF_IN_PUPD;
    }

    *reg = (*reg & ~(0xFUL << shift)) | (cfg << shift);
}

void gpio_init(gpio_t pin, gpio_mode_t mode, gpio_speed_t speed)
{
    gpio_set_mode(pin.port, pin.pin, mode, speed);
}

void gpio_write(gpio_t pin, int level)
{
    if (level)
    {
        pin.port->BSRR = (1UL << pin.pin);
    }
    else
    {
        pin.port->BRR = (1UL << pin.pin);
    }
}

int gpio_read(gpio_t pin)
{
    return (pin.port->IDR & (1UL << pin.pin)) ? 1 : 0;
}

void gpio_toggle(gpio_t pin)
{
    pin.port->ODR ^= (1UL << pin.pin);
}

void gpio_write_mask(GPIO_TypeDef *port, uint16_t mask, int level)
{
    port->BSRR = level ? mask : (mask << 16);
}
