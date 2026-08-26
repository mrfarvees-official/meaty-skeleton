#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/rtc.h>
#include <kernel/system_time.h>


/*
 * Sri Jayawardenepura Kotte / Sri Lanka
 *
 * UTC + 05:30
 */
#define SYSTEM_TIME_DEFAULT_UTC_OFFSET_MINUTES \
    (5 * 60 + 30)


static int32_t utc_offset_minutes =
    SYSTEM_TIME_DEFAULT_UTC_OFFSET_MINUTES;


static bool system_time_is_leap_year(
    uint16_t year)
{
    if ((year % 400u) == 0u)
        return true;

    if ((year % 100u) == 0u)
        return false;

    return
        (year % 4u) == 0u;
}


static uint8_t system_time_days_in_month(
    uint16_t year,
    uint8_t month)
{
    static const uint8_t days[] =
    {
        31u,
        28u,
        31u,
        30u,
        31u,
        30u,
        31u,
        31u,
        30u,
        31u,
        30u,
        31u
    };

    if (month < 1u ||
        month > 12u)
    {
        return 0u;
    }

    if (month == 2u &&
        system_time_is_leap_year(year))
    {
        return 29u;
    }

    return
        days[month - 1u];
}


static void system_time_increment_day(
    rtc_datetime_t *datetime)
{
    uint8_t days_in_month =
        system_time_days_in_month(
            datetime->year,
            datetime->month);

    ++datetime->day;

    if (datetime->day <= days_in_month)
        return;

    datetime->day = 1u;

    ++datetime->month;

    if (datetime->month <= 12u)
        return;

    datetime->month = 1u;

    ++datetime->year;
}


static void system_time_decrement_day(
    rtc_datetime_t *datetime)
{
    if (datetime->day > 1u)
    {
        --datetime->day;
        return;
    }

    if (datetime->month > 1u)
    {
        --datetime->month;
    }
    else
    {
        datetime->month = 12u;

        if (datetime->year > 0u)
            --datetime->year;
    }

    datetime->day =
        system_time_days_in_month(
            datetime->year,
            datetime->month);
}


bool system_time_local_datetime(
    rtc_datetime_t *result)
{
    if (result == NULL)
        return false;

    rtc_datetime_t datetime;

    if (!rtc_read_datetime(
            &datetime))
    {
        return false;
    }

    int32_t total_minutes =
        (int32_t)datetime.hour * 60 +
        (int32_t)datetime.minute +
        utc_offset_minutes;

    while (total_minutes >= 24 * 60)
    {
        total_minutes -=
            24 * 60;

        system_time_increment_day(
            &datetime);
    }

    while (total_minutes < 0)
    {
        total_minutes +=
            24 * 60;

        system_time_decrement_day(
            &datetime);
    }

    datetime.hour =
        (uint8_t)
        (total_minutes / 60);

    datetime.minute =
        (uint8_t)
        (total_minutes % 60);

    *result =
        datetime;

    return true;
}


int32_t system_time_utc_offset_minutes(void)
{
    return
        utc_offset_minutes;
}


void system_time_set_utc_offset_minutes(
    int32_t offset_minutes)
{
    /*
     * Keep the range within real-world UTC offset limits.
     */
    if (offset_minutes < -14 * 60 ||
        offset_minutes > 14 * 60)
    {
        return;
    }

    utc_offset_minutes =
        offset_minutes;
}