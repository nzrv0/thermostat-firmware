/*
 * fake_i2c.h - a simulated I2C device for the host tests.
 *
 * It decodes the master's bit stream from the SCL/SDA edges and answers
 * like an AT24C256: 0xA0 sets the address pointer, 0xA1 streams data
 * back, and every accepted byte is acknowledged.
 */

#ifndef FAKE_I2C_H
#define FAKE_I2C_H

#include <stdint.h>
#include "i2c.h"

typedef struct
{
    uint8_t mem[256];
    uint16_t pointer;
    gpio_t sda;
    gpio_t scl;

    int state;
    int bit_count;
    uint8_t shift;
    uint8_t current_bit;
    int bit_index;
    int own_sda;
    int sda_level;
    int scl_level;
    int master_sda;
    int started;
    int stopped;
    int nack_count;
    int ack_count;
    int phase;
    int send_next;
    int ack_slot_done;
    int armed;
} fake_i2c_t;

/* Binds a bus to the fake device. The bus's line callbacks are replaced
 * with the fake's edge decoder. */
void fake_i2c_init(fake_i2c_t *fake, i2c_bus_t *bus);

#endif /* FAKE_I2C_H */
