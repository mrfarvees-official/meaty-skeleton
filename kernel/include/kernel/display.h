#ifndef KERNEL_DISPLAY_H
#define KERNEL_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct display_mode
{
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;

} display_mode_t;

typedef struct display_capabilities
{
    uint32_t max_width;
    uint32_t max_height;
    uint32_t max_bpp;

    size_t video_memory_bytes;

} display_capabilities_t;

/*
 * Detect the QEMU/Bochs VBE runtime display interface.
 *
 * The Multiboot framebuffer must already have been initialized before
 * calling this.
 */
bool display_initialize(void);

bool display_is_available(void);

bool display_get_capabilities(
    display_capabilities_t *capabilities);

bool display_get_mode(
    display_mode_t *mode);

/*
 * Set a runtime display mode.
 *
 * The device may adjust the requested dimensions.
 *
 * In particular QEMU standard VGA requires horizontal resolution to
 * be a multiple of 8.
 *
 * On success, accepted_mode receives the actual mode.
 */
bool display_set_mode(
    uint32_t width,
    uint32_t height,
    uint32_t bpp,
    display_mode_t *accepted_mode);

#endif