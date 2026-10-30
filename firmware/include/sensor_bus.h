/*
 * sensor_bus.h - the two-wire bus to the external measurement device.
 *
 * The GPIOC pins PC8 (data) and PC9 (clock) carry a second, clocked
 * two-wire bus that is not I2C: the original drives both lines as
 * push-pull outputs and never uses the acknowledge slot. The primitives
 * live at 0x08061958 (read byte), 0x08061B1C (write byte), 0x08061A50 /
 * 0x080619BC (start/stop) and 0x08061A80 / 0x08061AE0 (acknowledge).
 *
 * The device on this bus answers the command bytes 0x88, 0xFD and 0x89
 * with six bytes - three 16-bit samples - which the firmware converts
 * with the NTC divider formula (0x08062504).
 */

#ifndef SENSOR_BUS_H
#define SENSOR_BUS_H

#include <stdint.h>
#include "gpio.h"

typedef struct
{
    gpio_t data;
    gpio_t clock;
    void (*set_line)(gpio_t pin, int level);
    int  (*get_line)(gpio_t pin);
    void (*delay)(void);
} sensor_bus_t;

void sensor_bus_init(sensor_bus_t *bus, gpio_t data, gpio_t clock);

void sensor_bus_start(sensor_bus_t *bus);
void sensor_bus_stop(sensor_bus_t *bus);

/* One byte, MSB first. The clock idles low and the data line is set
 * before the clock is pulsed, exactly as the original does it. */
void sensor_bus_write_byte(sensor_bus_t *bus, uint8_t byte);
uint8_t sensor_bus_read_byte(sensor_bus_t *bus);

/* The sequence the original uses to fetch the three channels: two
 * command bytes, a poll byte and then six data bytes. Returns the number
 * of bytes read. */
uint8_t sensor_bus_read_samples(sensor_bus_t *bus, uint8_t *buf, uint8_t len);

#endif /* SENSOR_BUS_H */
