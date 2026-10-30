/*
 * rtc.h - the time source.
 *
 * The firmware keeps no RTC device on any bus it drives (see
 * docs/open-questions.md question 1): there is no I2C peripheral in the
 * image at all, and the S35390 is not reached over the bit-banged bus
 * on GPIOG, the two-wire bus on GPIOC or the ADC.
 *
 * What it does instead is hold the time in RAM:
 *
 *   - the date and time shadow at 0x200021c8, of which offset 0x14 is
 *     written by the function at 0x0807e260, and
 *   - the raw time words at 0x20000100..0x2000010c, read by the
 *     control function at 0x0807f9bc.
 *
 * The time therefore arrives from outside the local drivers. The
 * leading candidate is the protocol slave path: the dispatcher at
 * 0x080758e8 writes the humidity byte at 0x20002248 + 0x291, and it is
 * the function the Modbus and BACnet servers publish their points
 * through.
 *
 * This module keeps the S35390 register model, so the same code can be
 * pointed at a real bus, and binds it to the RAM shadow the firmware
 * uses.
 */

#ifndef RTC_H
#define RTC_H

#include <stdint.h>

/* The S35390 registers the firmware's data model uses. */
#define S35390_SECONDS       0x00
#define S35390_MINUTES       0x01
#define S35390_HOURS         0x02
#define S35390_DAY           0x03
#define S35390_WEEKDAY       0x04
#define S35390_MONTH         0x05
#define S35390_YEAR          0x06
#define S35390_TEMPERATURE   0x12

/* The time, as the firmware keeps it. */
typedef struct
{
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;        /* 1..31 */
    uint8_t weekday;    /* 0..6, 0 = Sunday */
    uint8_t month;      /* 1..12 */
    uint16_t year;      /* full year, e.g. 2026 */
} rtc_time_t;

/* The state the driver keeps. */
typedef struct
{
    rtc_time_t time;
    uint8_t    valid;    /* 1 when the time has been set */
    uint8_t    battery_low;
} rtc_t;

/* Binds the driver to the RAM shadow the firmware uses: `shadow` is
 * the date and time block and `raw` the four raw time words. Both may
 * be NULL, in which case the driver keeps the time internally. */
void rtc_init(rtc_t *rtc, uint8_t *shadow, uint32_t *raw);

/* Sets the time. */
void rtc_set_time(rtc_t *rtc, const rtc_time_t *time);

/* Reads the time. Returns 0 when the time has never been set. */
int rtc_get_time(rtc_t *rtc, rtc_time_t *time);

/* The time as the firmware stores it: the seconds since midnight, the
 * minute of the hour, the hour of the day and the day of the year in
 * the four raw words. */
void rtc_split(const rtc_time_t *time, uint16_t *seconds_of_day,
               uint16_t *minute_of_hour, uint16_t *hour_of_day,
               uint16_t *day_of_year);

/* The day of the year, 1..366. */
uint16_t rtc_day_of_year(const rtc_time_t *time);

/* Is the year a leap year? */
int rtc_is_leap_year(uint16_t year);

#endif /* RTC_H */
