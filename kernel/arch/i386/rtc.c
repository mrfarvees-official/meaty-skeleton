#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/rtc.h>

#include "io.h"


#define CMOS_INDEX_PORT \
    0x70u

#define CMOS_DATA_PORT \
    0x71u


#define CMOS_REGISTER_SECONDS \
    0x00u

#define CMOS_REGISTER_MINUTES \
    0x02u

#define CMOS_REGISTER_HOURS \
    0x04u

#define CMOS_REGISTER_DAY \
    0x07u

#define CMOS_REGISTER_MONTH \
    0x08u

#define CMOS_REGISTER_YEAR \
    0x09u

#define CMOS_REGISTER_STATUS_A \
    0x0Au

#define CMOS_REGISTER_STATUS_B \
    0x0Bu


#define CMOS_STATUS_A_UPDATE_IN_PROGRESS \
    0x80u

#define CMOS_STATUS_B_24_HOUR \
    0x02u

#define CMOS_STATUS_B_BINARY \
    0x04u

#define CMOS_HOUR_PM \
    0x80u


#define CMOS_UPDATE_WAIT_LIMIT \
    100000u

#define CMOS_STABLE_READ_LIMIT \
    8u


typedef struct rtc_cmos_sample
{
    uint8_t second;
    uint8_t minute;
    uint8_t hour;

    uint8_t day;
    uint8_t month;
    uint8_t year;

} rtc_cmos_sample_t;


static uint8_t rtc_cmos_read(
    uint8_t register_index)
{
    /*
     * Preserve the current NMI-disable bit while selecting a CMOS
     * register.
     */
    uint8_t nmi_mask =
        inb(CMOS_INDEX_PORT) &
        0x80u;

    outb(
        CMOS_INDEX_PORT,
        nmi_mask |
        (register_index & 0x7Fu));

    return
        inb(CMOS_DATA_PORT);
}


static bool rtc_cmos_wait_ready(void)
{
    for (uint32_t attempt = 0u;
         attempt < CMOS_UPDATE_WAIT_LIMIT;
         ++attempt)
    {
        uint8_t status_a =
            rtc_cmos_read(
                CMOS_REGISTER_STATUS_A);

        if ((status_a &
             CMOS_STATUS_A_UPDATE_IN_PROGRESS) == 0u)
        {
            return true;
        }
    }

    return false;
}


static void rtc_cmos_read_sample(
    rtc_cmos_sample_t *sample)
{
    sample->second =
        rtc_cmos_read(
            CMOS_REGISTER_SECONDS);

    sample->minute =
        rtc_cmos_read(
            CMOS_REGISTER_MINUTES);

    sample->hour =
        rtc_cmos_read(
            CMOS_REGISTER_HOURS);

    sample->day =
        rtc_cmos_read(
            CMOS_REGISTER_DAY);

    sample->month =
        rtc_cmos_read(
            CMOS_REGISTER_MONTH);

    sample->year =
        rtc_cmos_read(
            CMOS_REGISTER_YEAR);
}


static bool rtc_cmos_samples_equal(
    const rtc_cmos_sample_t *first,
    const rtc_cmos_sample_t *second)
{
    return
        first->second == second->second &&
        first->minute == second->minute &&
        first->hour == second->hour &&
        first->day == second->day &&
        first->month == second->month &&
        first->year == second->year;
}


static uint8_t rtc_bcd_to_binary(
    uint8_t value)
{
    return
        (uint8_t)
        ((value & 0x0Fu) +
         ((value >> 4u) * 10u));
}


static bool rtc_datetime_valid(
    const rtc_datetime_t *datetime)
{
    if (datetime == NULL)
        return false;

    if (datetime->month < 1u ||
        datetime->month > 12u)
    {
        return false;
    }

    if (datetime->day < 1u ||
        datetime->day > 31u)
    {
        return false;
    }

    if (datetime->hour > 23u)
        return false;

    if (datetime->minute > 59u)
        return false;

    if (datetime->second > 59u)
        return false;

    return true;
}


bool rtc_read_datetime(
    rtc_datetime_t *result)
{
    if (result == NULL)
        return false;

    rtc_cmos_sample_t first;
    rtc_cmos_sample_t second;

    bool stable =
        false;

    for (uint32_t attempt = 0u;
         attempt < CMOS_STABLE_READ_LIMIT;
         ++attempt)
    {
        if (!rtc_cmos_wait_ready())
            return false;

        rtc_cmos_read_sample(
            &first);

        if (!rtc_cmos_wait_ready())
            return false;

        rtc_cmos_read_sample(
            &second);

        if (rtc_cmos_samples_equal(
                &first,
                &second))
        {
            stable =
                true;

            break;
        }
    }

    if (!stable)
        return false;

    uint8_t status_b =
        rtc_cmos_read(
            CMOS_REGISTER_STATUS_B);

    bool binary_mode =
        (status_b &
         CMOS_STATUS_B_BINARY) != 0u;

    bool twenty_four_hour_mode =
        (status_b &
         CMOS_STATUS_B_24_HOUR) != 0u;

    bool pm =
        (second.hour &
         CMOS_HOUR_PM) != 0u;

    uint8_t hour =
        second.hour &
        (uint8_t)~CMOS_HOUR_PM;

    uint8_t second_value =
        second.second;

    uint8_t minute =
        second.minute;

    uint8_t day =
        second.day;

    uint8_t month =
        second.month;

    uint8_t year =
        second.year;

    if (!binary_mode)
    {
        second_value =
            rtc_bcd_to_binary(
                second_value);

        minute =
            rtc_bcd_to_binary(
                minute);

        hour =
            rtc_bcd_to_binary(
                hour);

        day =
            rtc_bcd_to_binary(
                day);

        month =
            rtc_bcd_to_binary(
                month);

        year =
            rtc_bcd_to_binary(
                year);
    }

    if (!twenty_four_hour_mode)
    {
        if (pm)
        {
            if (hour != 12u)
                hour =
                    (uint8_t)(hour + 12u);
        }
        else
        {
            if (hour == 12u)
                hour = 0u;
        }
    }

    rtc_datetime_t datetime;

    /*
     * The legacy CMOS year register contains only two digits.
     *
     * Until we consume the FADT century-register field, Meaty OS
     * treats it as 2000..2099.
     */
    datetime.year =
        (uint16_t)
        (2000u + year);

    datetime.month =
        month;

    datetime.day =
        day;

    datetime.hour =
        hour;

    datetime.minute =
        minute;

    datetime.second =
        second_value;

    if (!rtc_datetime_valid(
            &datetime))
    {
        return false;
    }

    *result =
        datetime;

    return true;
}