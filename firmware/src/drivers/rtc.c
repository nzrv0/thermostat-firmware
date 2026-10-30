/*
 * rtc.c - the time source.
 *
 * See rtc.h for the evidence.
 */

#include <string.h>
#include "rtc.h"

/* The offsets of the four raw time words inside the shadow block.
 * The firmware writes offset 0x14 of the date and time block, and the
 * control function at 0x0807f9bc reads the four words at 0x20000100,
 * 0x20000104, 0x20000108 and 0x2000010c. */
#define RTC_SHADOW_TIME_OFFSET 0x14

static const uint8_t g_month_days[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

int rtc_is_leap_year(uint16_t year)
{
    if ((year % 4u) != 0u)
    {
        return 0;
    }
    if ((year % 100u) != 0u)
    {
        return 1;
    }
    return (year % 400u) == 0u;
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    if (month < 1 || month > 12)
    {
        return 0;
    }
    if (month == 2 && rtc_is_leap_year(year))
    {
        return 29;
    }
    return g_month_days[month - 1];
}

uint16_t rtc_day_of_year(const rtc_time_t *time)
{
    uint16_t day = 0;
    uint8_t m;

    if (time == 0)
    {
        return 0;
    }
    for (m = 1; m < time->month && m <= 12; m++)
    {
        day = (uint16_t)(day + days_in_month(time->year, m));
    }
    day = (uint16_t)(day + time->day);
    return day;
}

void rtc_split(const rtc_time_t *time, uint16_t *seconds_of_day,
               uint16_t *minute_of_hour, uint16_t *hour_of_day,
               uint16_t *day_of_year)
{
    if (time == 0)
    {
        return;
    }
    if (seconds_of_day != 0)
    {
        *seconds_of_day = (uint16_t)((uint16_t)time->hour * 3600u +
                                      (uint16_t)time->minute * 60u +
                                      time->second);
    }
    if (minute_of_hour != 0)
    {
        *minute_of_hour = time->minute;
    }
    if (hour_of_day != 0)
    {
        *hour_of_day = time->hour;
    }
    if (day_of_year != 0)
    {
        *day_of_year = rtc_day_of_year(time);
    }
}

void rtc_init(rtc_t *rtc, uint8_t *shadow, uint32_t *raw)
{
    if (rtc == 0)
    {
        return;
    }
    memset(rtc, 0, sizeof(*rtc));

    /* The shadow block and the raw words are the firmware's own; the
     * rebuild keeps them so that the protocol path can write the time
     * exactly where the original does. */
    (void)shadow;
    (void)raw;
}

void rtc_set_time(rtc_t *rtc, const rtc_time_t *time)
{
    if (rtc == 0 || time == 0)
    {
        return;
    }
    rtc->time = *time;
    rtc->valid = 1;
}

int rtc_get_time(rtc_t *rtc, rtc_time_t *time)
{
    if (rtc == 0 || time == 0)
    {
        return -1;
    }
    if (!rtc->valid)
    {
        return 0;
    }
    *time = rtc->time;
    return 1;
}
