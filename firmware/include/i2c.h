/*
 * i2c.h - bit-banged I2C master.
 *
 * The shipped firmware has no I2C peripheral: the AT24C256 EEPROM hangs
 * off a software-bit-banged bus on GPIOG - SDA = PG6 (mask 0x40), SCL =
 * PG8 (mask 0x100). The original's implementation is at 0x08042A08 -
 * 0x08042C88 (send byte 0x08042BD8, read byte 0x080429B0, start/stop and
 * acknowledge 0x08042AAC-0x08042B38), and the EEPROM read API is
 * 0x08042938.
 *
 * The bus is expressed through line callbacks so the protocol can be
 * exercised on the host against a simulated device; on the target the
 * callbacks are the GPIO helpers.
 */

#ifndef I2C_H
#define I2C_H

#include <stdint.h>
#include "gpio.h"

typedef struct
{
    gpio_t sda;
    gpio_t scl;
    void (*set_line)(gpio_t pin, int level);
    int  (*get_line)(gpio_t pin);
    void (*delay)(void);
} i2c_bus_t;

/* Binds the bus to the two pins and installs the GPIO-backed line
 * accessors. */
void i2c_bus_init(i2c_bus_t *bus, gpio_t sda, gpio_t scl);

/* START: SDA goes low while SCL is high. */
void i2c_start(i2c_bus_t *bus);

/* STOP: SDA goes high while SCL is high. */
void i2c_stop(i2c_bus_t *bus);

/* Sends one byte MSB first. Returns 1 when the device acknowledged. */
int i2c_write_byte(i2c_bus_t *bus, uint8_t byte);

/* Reads one byte MSB first. ack != 0 pulls SDA low to acknowledge it. */
uint8_t i2c_read_byte(i2c_bus_t *bus, int ack);

#endif /* I2C_H */
