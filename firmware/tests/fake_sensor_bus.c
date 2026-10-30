/*
 * fake_sensor_bus.c - a simulated device for the two-wire measurement
 * bus.
 *
 * It decodes the master's bytes from the clock edges and, once the
 * master has stopped driving the data line, streams the sample bytes.
 */

#include "fake_sensor_bus.h"

enum
{
    ST_IDLE = 0,
    ST_RECV,
    ST_SEND
};

static fake_sensor_bus_t *g_fake;

static void fake_set_line(gpio_t pin, int level);
static int fake_get_line(gpio_t pin);

void fake_sensor_bus_init(fake_sensor_bus_t *fake, sensor_bus_t *bus)
{
    int i;

    for (i = 0; i < 256; i++)
    {
        fake->samples[i] = 0;
    }
    fake->data = bus->data;
    fake->clock = bus->clock;
    fake->state = ST_IDLE;
    fake->bit_count = 0;
    fake->shift = 0;
    fake->bit_index = 0;
    fake->byte_index = 0;
    fake->own_data = 0;
    fake->data_level = 1;
    fake->clock_level = 1;
    fake->master_data = 1;
    fake->started = 0;
    fake->stopped = 0;
    fake->bytes_received = 0;
    fake->last_byte = 0;

    g_fake = fake;

    bus->set_line = fake_set_line;
    bus->get_line = fake_get_line;
    bus->delay = 0;
}

static void fake_drive(fake_sensor_bus_t *fake, int level)
{
    fake->own_data = 1;
    fake->data_level = level;
}

static void fake_release(fake_sensor_bus_t *fake)
{
    fake->own_data = 0;
    fake->data_level = 1;
}

static void fake_on_rising(fake_sensor_bus_t *fake)
{
    switch (fake->state)
    {
    case ST_RECV:
        fake->shift = (uint8_t)((fake->shift << 1) |
                                (fake->master_data ? 1 : 0));
        fake->bit_count++;
        if (fake->bit_count == 8)
        {
            fake->last_byte = fake->shift;
            fake->bytes_received++;
            fake->bit_count = 0;
            fake->shift = 0;
        }
        break;

    case ST_SEND:
        fake_drive(fake, (fake->samples[fake->byte_index] >>
                           (7 - fake->bit_index)) & 1);
        fake->bit_index++;
        if (fake->bit_index == 8)
        {
            fake->bit_index = 0;
            fake->byte_index = (fake->byte_index + 1) & 0xFF;
        }
        break;

    default:
        break;
    }
}

static void fake_set_line(gpio_t pin, int level)
{
    fake_sensor_bus_t *fake = g_fake;
    int was_clock;

    if (fake == 0)
    {
        return;
    }

    if (pin.pin == fake->data.pin)
    {
        was_clock = fake->clock_level;
        if (level < fake->master_data && was_clock)
        {
            fake->started++;
            fake->state = ST_RECV;
            fake->bit_count = 0;
            fake->shift = 0;
            fake_release(fake);
        }
        else if (level > fake->master_data && was_clock)
        {
            fake->stopped++;
            fake->state = ST_SEND;
            fake->bit_index = 0;
            fake->byte_index = 0;
            fake_release(fake);
        }
        fake->master_data = level;
        return;
    }

    if (level && !fake->clock_level)
    {
        fake_on_rising(fake);
    }
    fake->clock_level = level;
}

static int fake_get_line(gpio_t pin)
{
    fake_sensor_bus_t *fake = g_fake;

    if (fake == 0)
    {
        return 1;
    }
    if (pin.pin == fake->data.pin && fake->own_data)
    {
        return fake->data_level;
    }
    return fake->master_data;
}
