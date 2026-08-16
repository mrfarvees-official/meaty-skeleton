#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/paging.h>
#include <kernel/usercopy.h>

static bool user_range_accessible(
    uintptr_t address,
    size_t length,
    bool writable)
{
    if (length == 0)
        return true;

    /*
     * Reject wraparound before calculating the final byte.
     */
    if (address >
        UINTPTR_MAX - (length - 1u))
    {
        return false;
    }

    uintptr_t last =
        address + length - 1u;

    uintptr_t page =
        address &
        ~(uintptr_t)(PAGE_SIZE - 1u);

    uintptr_t last_page =
        last &
        ~(uintptr_t)(PAGE_SIZE - 1u);

    for (;;)
    {
        uint32_t flags = 0;

        if (!paging_get_effective_flags(
                page,
                &flags))
        {
            return false;
        }

        if ((flags & PAGE_USER) == 0)
            return false;

        if (writable &&
            (flags & PAGE_WRITABLE) == 0)
        {
            return false;
        }

        if (page == last_page)
            break;

        if (page >
            UINTPTR_MAX - PAGE_SIZE)
        {
            return false;
        }

        page += PAGE_SIZE;
    }

    return true;
}

bool copy_from_user(
    void *kernel_destination,
    const void *user_source,
    size_t length)
{
    if (length == 0)
        return true;

    if (kernel_destination == NULL ||
        user_source == NULL)
    {
        return false;
    }

    uintptr_t source =
        (uintptr_t)user_source;

    if (!user_range_accessible(
            source,
            length,
            false))
    {
        return false;
    }

    memcpy(
        kernel_destination,
        user_source,
        length);

    return true;
}

bool copy_to_user(
    void *user_destination,
    const void *kernel_source,
    size_t length)
{
    if (length == 0)
        return true;

    if (user_destination == NULL ||
        kernel_source == NULL)
    {
        return false;
    }

    uintptr_t destination =
        (uintptr_t)user_destination;

    if (!user_range_accessible(
            destination,
            length,
            true))
    {
        return false;
    }

    memcpy(
        user_destination,
        kernel_source,
        length);

    return true;
}