/*
 * meter.h - energy metering.
 *
 * The firmware publishes "Running Time(%dHrs)" and two power figures,
 * "Power consumption(%d.%dKw)" and "Power consumption(%d.%d$)"
 * (0x08081C50-0x08081C80).
 */

#ifndef METER_H
#define METER_H

#include <stdint.h>

typedef struct
{
    uint32_t run_seconds;      /* seconds the load has been running */
    uint32_t energy_watt_seconds; /* integral of the load power */
    uint32_t peak_watts;       /* highest instantaneous power seen */
} meter_t;

void meter_reset(meter_t *meter);

/* Accounts one sample: the load ran for the given number of seconds at
 * the given power. */
void meter_accumulate(meter_t *meter, uint32_t seconds, uint32_t watts);

/* Average power in watts, zero when the meter has not run. */
uint32_t meter_average_watts(const meter_t *meter);

/* Average power in kilowatts with one decimal: the firmware prints it
 * as "%d.%dKw". */
void meter_power_kw(const meter_t *meter, int *whole, int *tenths);

/* Running time in hours, the unit the firmware prints. */
uint32_t meter_running_hours(const meter_t *meter);

#endif /* METER_H */
