#ifndef USER_LINK_DESCRIPTOR_H
#define USER_LINK_DESCRIPTOR_H

#include <stddef.h>

#define LINK_DESCRIPTOR_PATH_MAX 256u

typedef struct link_descriptor
{
    char application[
        LINK_DESCRIPTOR_PATH_MAX];
} link_descriptor_t;


/*
 * Load and validate one Meaty OS link descriptor.
 *
 * Format:
 *
 * [Link]
 * Application=/apps/terminal.app
 *
 * Returns:
 *      0 success
 *     -1 failure
 */
int link_descriptor_load(
    const char *path,
    link_descriptor_t *descriptor);

#endif