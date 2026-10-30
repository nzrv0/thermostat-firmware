/*
 * schedule.h - temperature schedule, hold and holiday.
 *
 * The firmware's schedule is a list of periods with a start time and a
 * setpoint, and the day has 1440 minutes ("1440" is a literal in the
 * image at 0x08067214). Hold and holiday are a pair of end times:
 * "Hold End tTime", "Holiday Start tTime", "Holiday End tTime".
 *
 * Times are minutes since midnight and setpoints are tenths of a degree,
 * the unit the firmware uses everywhere.
 */

#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <stdint.h>

#define SCHEDULE_MINUTES_PER_DAY   1440
#define SCHEDULE_MAX_PERIODS       8
#define SCHEDULE_NO_TIME           0xFFFFU

typedef struct
{
    uint16_t start;      /* minutes since midnight */
    uint16_t setpoint;   /* tenths of a degree */
} schedule_period_t;

typedef struct
{
    schedule_period_t period[SCHEDULE_MAX_PERIODS];
    uint8_t  count;
    uint16_t hold_end;
    uint16_t holiday_start;
    uint16_t holiday_end;
} schedule_t;

void schedule_init(schedule_t *schedule);

/* Adds or replaces the period that starts at the given minute.
 * Returns 0 on success. */
int schedule_set_period(schedule_t *schedule, uint16_t start, uint16_t setpoint);

/* The setpoint in force at the given minute: the last period that has
 * already started, wrapping around midnight. */
uint16_t schedule_setpoint_at(const schedule_t *schedule, uint16_t minute);

int schedule_in_hold(const schedule_t *schedule, uint16_t minute);
int schedule_in_holiday(const schedule_t *schedule, uint16_t minute);

/* Minutes since midnight, clamped into the day. */
uint16_t schedule_minute_of_day(uint8_t hour, uint8_t minute);

#endif /* SCHEDULE_H */
