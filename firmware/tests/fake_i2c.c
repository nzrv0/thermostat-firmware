/*
 * fake_i2c.c - a simulated I2C device for the host tests.
 *
 * The device models an AT24C256: 0xA0 selects write mode and is followed
 * by a two-byte address, 0xA1 switches to read mode and streams the bytes
 * from the pointer. Every accepted byte is acknowledged.
 */

#include "fake_i2c.h"

enum
{
    ST_IDLE = 0,
    ST_RECV,      /* collecting a byte from the master */
    ST_ACK,       /* the acknowledge slot: our line is driven low */
    ST_SEND,      /* streaming a byte to the master */
    ST_READ_ACK   /* waiting for the master's acknowledge */
};

enum
{
    PH_DEVICE = 0,
    PH_ADDR_HI,
    PH_ADDR_LO,
    PH_DATA
};

static fake_i2c_t *g_fake;

static void fake_set_line(gpio_t pin, int level);
static int fake_get_line(gpio_t pin);

void fake_i2c_init(fake_i2c_t *fake, i2c_bus_t *bus)
{
    int i;

    for (i = 0; i < 256; i++)
    {
        fake->mem[i] = 0;
    }
    fake->pointer = 0;
    fake->sda = bus->sda;
    fake->scl = bus->scl;
    fake->state = ST_IDLE;
    fake->phase = PH_DEVICE;
    fake->bit_count = 0;
    fake->shift = 0;
    fake->current_bit = 0;
    fake->bit_index = 0;
    fake->own_sda = 0;
    fake->sda_level = 1;
    fake->scl_level = 1;
    fake->master_sda = 1;
    fake->started = 0;
    fake->stopped = 0;
    fake->nack_count = 0;
    fake->ack_count = 0;
    fake->send_next = 0;
    fake->ack_slot_done = 0;
    fake->armed = 0;

    g_fake = fake;

    bus->set_line = fake_set_line;
    bus->get_line = fake_get_line;
    bus->delay = 0;
}

static void fake_drive(fake_i2c_t *fake, int level)
{
    fake->own_sda = 1;
    fake->sda_level = level;
}

static void fake_release(fake_i2c_t *fake)
{
    fake->own_sda = 0;
    fake->sda_level = 1;
}

static void fake_present_bit(fake_i2c_t *fake)
{
    uint8_t byte = fake->mem[fake->pointer & 0xFF];

    fake_drive(fake, (byte >> (7 - fake->bit_index)) & 1);
}

static void fake_byte_received(fake_i2c_t *fake, uint8_t byte)
{
    fake->send_next = 0;

    switch (fake->phase)
    {
    case PH_DEVICE:
        if ((byte & 0xFE) == 0xA0)
        {
            if (byte & 1)
            {
                /* Read mode: stream from the pointer once acknowledged. */
                fake->send_next = 1;
            }
            else
            {
                fake->phase = PH_ADDR_HI;
            }
        }
        break;

    case PH_ADDR_HI:
        fake->pointer = (uint16_t)((uint16_t)byte << 8);
        fake->phase = PH_ADDR_LO;
        break;

    case PH_ADDR_LO:
        fake->pointer = (uint16_t)(fake->pointer | byte);
        fake->phase = PH_DATA;
        break;

    case PH_DATA:
        fake->mem[fake->pointer & 0xFF] = byte;
        fake->pointer = (uint16_t)((fake->pointer + 1) & 0xFFFF);
        break;

    default:
        break;
    }
}

static void fake_on_rising(fake_i2c_t *fake)
{
    switch (fake->state)
    {
    case ST_RECV:
        if (fake->bit_count < 8)
        {
            fake->shift = (uint8_t)((fake->shift << 1) |
                                    (fake->master_sda ? 1 : 0));
            fake->bit_count++;
            if (fake->bit_count == 8)
            {
                fake_byte_received(fake, fake->shift);
                fake_drive(fake, 0);
                fake->ack_count++;
                fake->ack_slot_done = 0;
                fake->state = ST_ACK;
            }
        }
        break;

    case ST_ACK:
        /* The master samples our low line during this slot. */
        fake->ack_slot_done = 1;
        break;

    case ST_SEND:
        /* The master samples the bit we are holding. */
        break;

    case ST_READ_ACK:
        if (fake->master_sda)
        {
            fake->nack_count++;
            fake->state = ST_IDLE;
        }
        else
        {
            fake->ack_count++;
            fake->pointer = (uint16_t)((fake->pointer + 1) & 0xFFFF);
            fake->bit_index = 0;
            fake_present_bit(fake);
            fake->armed = 0;
            fake->state = ST_SEND;
        }
        break;

    default:
        break;
    }
}

static void fake_on_falling(fake_i2c_t *fake)
{
    switch (fake->state)
    {
    case ST_ACK:
        if (!fake->ack_slot_done)
        {
            break;
        }
        fake_release(fake);
        fake->bit_count = 0;
        fake->shift = 0;
        if (fake->send_next)
        {
            fake->bit_index = 0;
            fake_present_bit(fake);
            fake->armed = 1;
            fake->state = ST_SEND;
        }
        else
        {
            fake->state = ST_RECV;
        }
        break;

    case ST_SEND:
        if (!fake->armed)
        {
            /* The falling edge that follows the master's acknowledge
             * clock does not advance the data bits. */
            fake->armed = 1;
            break;
        }
        if (fake->bit_index < 7)
        {
            fake->bit_index++;
            fake_present_bit(fake);
        }
        else
        {
            fake_release(fake);
            fake->state = ST_READ_ACK;
        }
        break;

    default:
        break;
    }
}

static void fake_set_line(gpio_t pin, int level)
{
    fake_i2c_t *fake = g_fake;
    int was_scl;

    if (fake == 0)
    {
        return;
    }

    if (pin.pin == fake->sda.pin)
    {
        was_scl = fake->scl_level;
        if (level < fake->master_sda && was_scl)
        {
            /* START: SDA falls while SCL is high. */
            fake->started++;
            fake->state = ST_RECV;
            fake->phase = PH_DEVICE;
            fake->bit_count = 0;
            fake->shift = 0;
            fake_release(fake);
        }
        else if (level > fake->master_sda && was_scl)
        {
            /* STOP: SDA rises while SCL is high. */
            fake->stopped++;
            fake->state = ST_IDLE;
            fake->phase = PH_DEVICE;
            fake_release(fake);
        }
        fake->master_sda = level;
        return;
    }

    if (level && !fake->scl_level)
    {
        fake_on_rising(fake);
    }
    else if (!level && fake->scl_level)
    {
        fake_on_falling(fake);
    }
    fake->scl_level = level;
}

static int fake_get_line(gpio_t pin)
{
    fake_i2c_t *fake = g_fake;

    if (fake == 0)
    {
        return 1;
    }
    if (pin.pin == fake->sda.pin && fake->own_sda)
    {
        return fake->sda_level;
    }
    return fake->master_sda;
}
