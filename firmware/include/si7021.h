/*
 * si7021.h - the humidity source.
 *
 * The SI7021 is a temperature and humidity sensor with a two-wire
 * interface. Like the S35390 (see rtc.h) the firmware does not reach it
 * over any bus it drives: there is no I2C peripheral in the image, and
 * the SI7021 does not appear on the bit-banged GPIOG bus or on the
 * two-wire GPIOC bus.
 *
 * Instead the humidity is a single byte in the state block:
 *
 *   0x20002248 + 0x291
 *
 * written by the protocol dispatcher at 0x080758e8 (three sites), by
 * 0x0807f9bc (two sites) and by 0x08080754 (one site). The self-test
 * reports a humidity *failure* message but never reads the sensor, so
 * the value is written from outside the local drivers.
 *
 * This module keeps the SI7021 command model, so the same code can be
 * pointed at a real bus, and binds it to the byte the firmware uses.
 */

#ifndef SI7021_H
#define SI7021_H

#include <stdint.h>

/* The SI7021 commands. */
#define SI7021_MEASURE_HUMIDITY_HOLD  0xE5
#define SI7021_MEASURE_TEMP_HOLD      0xE3
#define SI7021_READ_TEMP_PREV         0xE0
#define SI7021_RESET                  0xFE
#define SI7021_READ_ID_1              0xFA
#define SI7021_READ_ID_2              0xFC

/* The SI7021 reports the relative humidity as 12 bits: 6.25 % per
 * count, so the full scale is 100 % at 0x1000. */
#define SI7021_HUMIDITY_COUNTS  0x1000

typedef struct
{
    uint8_t  humidity;      /* tenths of a percent */
    uint8_t  valid;
} si7021_t;

/* Binds the driver to the byte the firmware keeps the humidity in. */
void si7021_init(si7021_t *si, uint8_t *storage);

/* Sets the humidity, in tenths of a percent. */
void si7021_set_humidity(si7021_t *si, int16_t humidity);

/* Reads the humidity, in tenths of a percent. Returns 0 when the value
 * has never been set. */
int si7021_get_humidity(si7021_t *si, int16_t *humidity);

/* Converts a raw SI7021 humidity count to tenths of a percent. */
int16_t si7021_counts_to_humidity(uint16_t counts);

#endif /* SI7021_H */
