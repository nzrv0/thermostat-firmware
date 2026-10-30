/*
 * schedule.c - temperature schedule, hold and holiday.
 */

#include <string.h>
#include "schedule.h"

void schedule_init(schedule_t *schedule)
{
    memset(schedule, 0, sizeof(*schedule));
    schedule->hold_end = SCHEDULE_NO_TIME;
    schedule->holiday_start = SCHEDULE_NO_TIME;
    schedule->holiday_end = SCHEDULE_NO_TIME;
}

int schedule_set_period(schedule_t *schedule, uint16_t start, uint16_t setpoint)
{
    uint8_t i;
    uint8_t slot = 0xFF;

    if (start >= SCHEDULE_MINUTES_PER_DAY)
    {
        return -1;
    }

    for (i = 0; i < schedule->count; i++)
    {
        if (schedule->period[i].start == start)
        {
            slot = i;
            break;
        }
    }

    if (slot == 0xFF)
    {
        if (schedule->count >= SCHEDULE_MAX_PERIODS)
        {
            return -1;
        }
        slot = schedule->count++;
        schedule->period[slot].start = start;
    }

    schedule->period[slot].setpoint = setpoint;
    return 0;
}

uint16_t schedule_setpoint_at(const schedule_t *schedule, uint16_t minute)
{
    uint16_t best = 0;
    int found = 0;
    uint8_t i;

    if (schedule->count == 0)
    {
        return 0;
    }

    for (i = 0; i < schedule->count; i++)
    {
        uint16_t start = schedule->period[i].start;

        if (start > minute)
        {
            continue;
        }
        if (!found || start >= best)
        {
            best = start;
            found = 1;
        }
    }

    if (!found)
    {
        /* Before the first period: the last period of the previous day. */
        best = schedule->period[0].start;
        for (i = 1; i < schedule->count; i++)
        {
            if (schedule->period[i].start >= best)
            {
                best = schedule->period[i].start;
            }
        }
    }

    for (i = 0; i < schedule->count; i++)
    {
        if (schedule->period[i].start == best)
        {
            return schedule->period[i].setpoint;
        }
    }
    return 0;
}

int schedule_in_hold(const schedule_t *schedule, uint16_t minute)
{
    if (schedule->hold_end == SCHEDULE_NO_TIME)
    {
        return 0;
    }
    return (minute < schedule->hold_end) ? 1 : 0;
}

int schedule_in_holiday(const schedule_t *schedule, uint16_t minute)
{
    if (schedule->holiday_start == SCHEDULE_NO_TIME ||
        schedule->holiday_end == SCHEDULE_NO_TIME)
    {
        return 0;
    }
    if (schedule->holiday_start <= schedule->holiday_end)
    {
        return (minute >= schedule->holiday_start && minute < schedule->holiday_end);
    }
    return (minute >= schedule->holiday_start || minute < schedule->holiday_end);
}

uint16_t schedule_minute_of_day(uint8_t hour, uint8_t minute)
{
    uint32_t value = (uint32_t)hour * 60u + minute;

    if (value >= SCHEDULE_MINUTES_PER_DAY)
    {
        value = SCHEDULE_MINUTES_PER_DAY - 1;
    }
    return (uint16_t)value;
}
