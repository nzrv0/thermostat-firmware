/*
 * si7021.c - the humidity source.
 *
 * See si7021.h for the evidence.
 */

#include <string.h>
#include "si7021.h"

void si7021_init(si7021_t *si, uint8_t *storage)
{
    if (si == 0)
    {
        return;
    }
    memset(si, 0, sizeof(*si));
    (void)storage;
}

void si7021_set_humidity(si7021_t *si, int16_t humidity)
{
    if (si == 0)
    {
        return;
    }
    if (humidity < 0)
    {
        humidity = 0;
    }
    if (humidity > 1000)
    {
        humidity = 1000;
    }
    si->humidity = (uint8_t)(humidity / 10u);
    si->valid = 1;
}

int si7021_get_humidity(si7021_t *si, int16_t *humidity)
{
    if (si == 0 || humidity == 0)
    {
        return -1;
    }
    if (!si->valid)
    {
        return 0;
    }
    *humidity = (int16_t)si->humidity * 10;
    return 1;
}

int16_t si7021_counts_to_humidity(uint16_t counts)
{
    /* The SI7021 reports the humidity in steps of 6.25 % per count,
     * so a count of 16 is 100 %. In tenths of a percent that is
     * counts * 6250 / 100. */
    int32_t tenths = ((int32_t)counts * 6250) / 100;

    if (tenths < 0)
    {
        tenths = 0;
    }
    if (tenths > 1000)
    {
        tenths = 1000;
    }
    return (int16_t)tenths;
}
