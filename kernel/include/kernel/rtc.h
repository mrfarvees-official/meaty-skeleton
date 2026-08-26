#ifndef KERNEL_RTC_H
#define KERNEL_RTC_H

#include <stdbool.h>
#include <stdint.h>


typedef struct rtc_datetime
{
    uint16_t year;

    uint8_t month;
    uint8_t day;

    uint8_t hour;
    uint8_t minute;
    uint8_t second;

} rtc_datetime_t;


/*
 * Read the platform real-time clock.
 *
 * Result uses:
 *
 *     Gregorian calendar
 *     month  1..12
 *     hour   0..23
 *
 * The current i386 CMOS backend maps the two-digit RTC year into
 * 2000..2099.
 */
bool rtc_read_datetime(
    rtc_datetime_t *result);


#endif