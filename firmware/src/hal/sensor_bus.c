/*
 * sensor_bus.c - the two-wire bus to the external measurement device.
 *
 * Both lines are push-pull outputs, as the original configures them
 * (0x08062464). The data line is released to a floating input while the
 * master reads, so the device can drive it.
 */

#include "sensor_bus.h"

static void bus_set(sensor_bus_t *bus, gpio_t pin, int level)
{
    if (bus->set_line)
    {
        bus->set_line(pin, level);
    }
}

static int bus_get(sensor_bus_t *bus, gpio_t pin)
{
    if (bus->get_line)
    {
        return bus->get_line(pin);
    }
    return 1;
}

static void bus_wait(sensor_bus_t *bus)
{
    if (bus->delay)
    {
        bus->delay();
    }
}

void sensor_bus_init(sensor_bus_t *bus, gpio_t data, gpio_t clock)
{
    bus->data = data;
    bus->clock = clock;
    bus->set_line = gpio_write;
    bus->get_line = gpio_read;
    bus->delay = 0;
}

void sensor_bus_start(sensor_bus_t *bus)
{
    bus_set(bus, bus->clock, 1);
    bus_wait(bus);
    bus_set(bus, bus->data, 0);
    bus_wait(bus);
    bus_set(bus, bus->clock, 0);
}

void sensor_bus_stop(sensor_bus_t *bus)
{
    bus_set(bus, bus->data, 0);
    bus_wait(bus);
    bus_set(bus, bus->clock, 1);
    bus_wait(bus);
    bus_set(bus, bus->data, 1);
    bus_wait(bus);
}

void sensor_bus_write_byte(sensor_bus_t *bus, uint8_t byte)
{
    int i;

    bus_set(bus, bus->clock, 0);
    for (i = 0; i < 8; i++)
    {
        bus_set(bus, bus->data, (byte & 0x80) ? 1 : 0);
        bus_wait(bus);
        bus_set(bus, bus->clock, 1);
        bus_wait(bus);
        bus_set(bus, bus->clock, 0);
        byte = (uint8_t)((byte << 1) | (byte >> 7));
    }
}

uint8_t sensor_bus_read_byte(sensor_bus_t *bus)
{
    uint8_t byte = 0;
    int i;

    bus_set(bus, bus->clock, 0);
    bus_set(bus, bus->data, 1);
    for (i = 0; i < 8; i++)
    {
        bus_set(bus, bus->clock, 1);
        bus_wait(bus);
        byte = (uint8_t)((byte << 1) | (bus_get(bus, bus->data) ? 1 : 0));
        bus_set(bus, bus->clock, 0);
        bus_wait(bus);
    }
    return byte;
}

uint8_t sensor_bus_read_samples(sensor_bus_t *bus, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    if (buf == 0 || len == 0)
    {
        return 0;
    }

    sensor_bus_start(bus);
    sensor_bus_write_byte(bus, 0x88);
    sensor_bus_stop(bus);

    sensor_bus_start(bus);
    sensor_bus_write_byte(bus, 0xFD);
    sensor_bus_stop(bus);

    for (i = 0; i < len; i++)
    {
        buf[i] = sensor_bus_read_byte(bus);
    }

    sensor_bus_stop(bus);
    return len;
}
