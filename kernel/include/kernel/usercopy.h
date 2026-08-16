#ifndef KERNEL_USERCOPY_H
#define KERNEL_USERCOPY_H

#include <stdbool.h>
#include <stddef.h>

bool copy_from_user(
    void *kernel_destination,
    const void *user_source,
    size_t length);

bool copy_to_user(
    void *user_destination,
    const void *kernel_source,
    size_t length);

#endif