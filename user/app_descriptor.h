#ifndef USER_APP_DESCRIPTOR_H
#define USER_APP_DESCRIPTOR_H

#include <stddef.h>

#define APP_DESCRIPTOR_NAME_MAX 64u
#define APP_DESCRIPTOR_PATH_MAX 256u

typedef struct app_descriptor
{
    char name[
        APP_DESCRIPTOR_NAME_MAX];

    char executable[
        APP_DESCRIPTOR_PATH_MAX];

    char icon[
        APP_DESCRIPTOR_PATH_MAX];
} app_descriptor_t;


/*
 * Load and validate one Meaty OS application descriptor.
 *
 * Required format:
 *
 * [Application]
 * Name=Terminal
 * Exec=/bin/sh.nex
 * Icon=/icons/apps/terminal.png
 * Type=Application
 *
 * Returns:
 *     0 success
 *    -1 failure
 */
int app_descriptor_load(
    const char *path,
    app_descriptor_t *descriptor);

#endif