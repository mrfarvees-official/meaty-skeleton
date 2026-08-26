#ifndef KERNEL_SYSTEM_TIME_H
#define KERNEL_SYSTEM_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/rtc.h>


bool system_time_local_datetime(
    rtc_datetime_t *result);

int32_t system_time_utc_offset_minutes(void);

void system_time_set_utc_offset_minutes(
    int32_t offset_minutes);


#endif