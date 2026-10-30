/*
 * meter.c - energy metering.
 */

#include <string.h>
#include "meter.h"

void meter_reset(meter_t *meter)
{
    memset(meter, 0, sizeof(*meter));
}

void meter_accumulate(meter_t *meter, uint32_t seconds, uint32_t watts)
{
    meter->run_seconds += seconds;
    meter->energy_watt_seconds += seconds * watts;
    if (watts > meter->peak_watts)
    {
        meter->peak_watts = watts;
    }
}

uint32_t meter_average_watts(const meter_t *meter)
{
    if (meter->run_seconds == 0)
    {
        return 0;
    }
    return meter->energy_watt_seconds / meter->run_seconds;
}

void meter_power_kw(const meter_t *meter, int *whole, int *tenths)
{
    uint32_t watts = meter_average_watts(meter);
    uint32_t milli = (watts * 1000u + 500u) / 1000u;

    if (whole != 0)
    {
        *whole = (int)(milli / 1000u);
    }
    if (tenths != 0)
    {
        *tenths = (int)((milli / 100u) % 10u);
    }
}

uint32_t meter_running_hours(const meter_t *meter)
{
    return meter->run_seconds / 3600u;
}
