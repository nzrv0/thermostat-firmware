/*
 * sensors.h - NTC and light sensor scaling.
 *
 * The original measures the thermistors with a resistive divider and
 * converts the counts to ohms before turning them into a temperature:
 *
 *   R = raw * 12500 / 65536 - 600      (12.5 kOhm pull-up)
 *   R clamped to 100 .. 9900
 *
 * That conversion is recovered from 0x08062504, which is the routine that
 * reads the external measurement device on GPIOC. The on-board channels
 * go through the same divider maths and then a lookup table in the
 * original's flash at 0x080845AE; this rebuild uses an equivalent curve
 * of its own (10 kOhm nominal, beta 3950), which is a rebuild choice and
 * not a recovered constant.
 *
 * Temperatures are carried in tenths of a degree, the unit the firmware
 * uses everywhere ("%d.%dc" in the self-test strings).
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

#define NTC_PULLUP_OHMS     12500u
#define NTC_ADC_FULL_SCALE  65536u
#define NTC_OFFSET_OHMS      600u
#define NTC_MIN_OHMS         100u
#define NTC_MAX_OHMS        9900u

/* Raw counts to ohms, as the original computes them. */
uint16_t ntc_resistance_from_raw(uint16_t raw);

/* Ohms to tenths of a degree Celsius. */
int16_t ntc_temperature_from_resistance(uint16_t ohms);

/* Raw counts to tenths of a degree Celsius in one step. */
int16_t ntc_temperature_from_raw(uint16_t raw);

#endif /* SENSORS_H */
