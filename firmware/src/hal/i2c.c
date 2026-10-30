/*
 * i2c.c - bit-banged I2C master.
 *
 * The timing follows the shipped implementation: the clock idles low, the
 * data line is set while the clock is low, and the clock is pulsed once
 * per bit. The original drives SDA as a push-pull output and releases it
 * to a floating input whenever it wants to read (GPIO configuration calls
 * at 0x08042A54 and 0x08042A80, with the pin masks 0x40 = PG6 for SDA and
 * 0x100 = PG8 for SCL).
 */

#include "i2c.h"

static void i2c_set(i2c_bus_t *bus, gpio_t pin, int level)
{
    if (bus->set_line)
    {
        bus->set_line(pin, level);
    }
}

static int i2c_get(i2c_bus_t *bus, gpio_t pin)
{
    if (bus->get_line)
    {
        return bus->get_line(pin);
    }
    return 1;
}

static void i2c_wait(i2c_bus_t *bus)
{
    if (bus->delay)
    {
        bus->delay();
    }
}

void i2c_bus_init(i2c_bus_t *bus, gpio_t sda, gpio_t scl)
{
    bus->sda = sda;
    bus->scl = scl;
    bus->set_line = gpio_write;
    bus->get_line = gpio_read;
    bus->delay = 0;
}

void i2c_start(i2c_bus_t *bus)
{
    i2c_set(bus, bus->sda, 1);
    i2c_set(bus, bus->scl, 1);
    i2c_wait(bus);
    i2c_set(bus, bus->sda, 0);
    i2c_wait(bus);
    i2c_set(bus, bus->scl, 0);
}

void i2c_stop(i2c_bus_t *bus)
{
    i2c_set(bus, bus->sda, 0);
    i2c_set(bus, bus->scl, 1);
    i2c_wait(bus);
    i2c_set(bus, bus->sda, 1);
    i2c_wait(bus);
}

int i2c_write_byte(i2c_bus_t *bus, uint8_t byte)
{
    int i;
    int ack;

    for (i = 0; i < 8; i++)
    {
        i2c_set(bus, bus->sda, (byte & 0x80) ? 1 : 0);
        i2c_wait(bus);
        i2c_set(bus, bus->scl, 1);
        i2c_wait(bus);
        i2c_set(bus, bus->scl, 0);
        byte = (uint8_t)((byte << 1) | (byte >> 7));
    }

    i2c_set(bus, bus->sda, 1);
    i2c_wait(bus);
    i2c_set(bus, bus->scl, 1);
    i2c_wait(bus);
    ack = i2c_get(bus, bus->sda) ? 0 : 1;
    i2c_set(bus, bus->scl, 0);
    i2c_wait(bus);

    return ack;
}

uint8_t i2c_read_byte(i2c_bus_t *bus, int ack)
{
    uint8_t byte = 0;
    int i;

    i2c_set(bus, bus->sda, 1);
    for (i = 0; i < 8; i++)
    {
        i2c_set(bus, bus->scl, 1);
        i2c_wait(bus);
        byte = (uint8_t)((byte << 1) | (i2c_get(bus, bus->sda) ? 1 : 0));
        i2c_set(bus, bus->scl, 0);
        i2c_wait(bus);
    }

    i2c_set(bus, bus->sda, ack ? 0 : 1);
    i2c_wait(bus);
    i2c_set(bus, bus->scl, 1);
    i2c_wait(bus);
    i2c_set(bus, bus->scl, 0);
    i2c_set(bus, bus->sda, 1);

    return byte;
}
