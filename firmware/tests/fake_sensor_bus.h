/*
 * fake_sensor_bus.h - a simulated device for the two-wire measurement
 * bus.
 */

#ifndef FAKE_SENSOR_BUS_H
#define FAKE_SENSOR_BUS_H

#include <stdint.h>
#include "sensor_bus.h"

typedef struct
{
    uint8_t samples[256];
    gpio_t data;
    gpio_t clock;

    int state;
    int bit_count;
    uint8_t shift;
    int bit_index;
    int byte_index;
    int own_data;
    int data_level;
    int clock_level;
    int master_data;
    int started;
    int stopped;
    int bytes_received;
    uint8_t last_byte;
} fake_sensor_bus_t;

void fake_sensor_bus_init(fake_sensor_bus_t *fake, sensor_bus_t *bus);

#endif /* FAKE_SENSOR_BUS_H */
