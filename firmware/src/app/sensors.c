/*
 * sensors.c - NTC and light sensor scaling.
 */

#include <math.h>
#include "sensors.h"

/* 10 kOhm thermistor, beta 3950, 25 degrees Celsius at 10 kOhm. */
#define NTC_NOMINAL_OHMS     10000
#define NTC_BETA             3950.0f
#define NTC_T0_KELVIN        298.15f
#define NTC_T0_OHMS          10000.0f

uint16_t ntc_resistance_from_raw(uint16_t raw)
{
    uint32_t value;

    value = (uint32_t)raw * NTC_PULLUP_OHMS / NTC_ADC_FULL_SCALE;
    value = (value > NTC_OFFSET_OHMS) ? (value - NTC_OFFSET_OHMS) : 0;

    if (value < NTC_MIN_OHMS)
    {
        value = NTC_MIN_OHMS;
    }
    else if (value > NTC_MAX_OHMS)
    {
        value = NTC_MAX_OHMS;
    }

    return (uint16_t)value;
}

int16_t ntc_temperature_from_resistance(uint16_t ohms)
{
    float r = (float)ohms;
    float t;

    if (r <= 0.0f)
    {
        r = 1.0f;
    }

    /* Beta equation, solved for T. */
    t = 1.0f / (1.0f / NTC_T0_KELVIN +
                logf(r / NTC_T0_OHMS) / NTC_BETA);
    t = t - 273.15f;

    return (int16_t)((t >= 0.0f) ? (t * 10.0f + 0.5f) : (t * 10.0f - 0.5f));
}

int16_t ntc_temperature_from_raw(uint16_t raw)
{
    return ntc_temperature_from_resistance(ntc_resistance_from_raw(raw));
}
