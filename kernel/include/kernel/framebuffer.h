#ifndef KERNEL_FRAMEBUFFER_H
#define KERNEL_FRAMEBUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FRAMEBUFFER_VIRTUAL_BASE  0xF0000000u
#define FRAMEBUFFER_VIRTUAL_LIMIT 0xFFC00000u

typedef struct framebuffer_boot_info
{
    uint64_t physical_address;

    uint32_t pitch;
    uint32_t width;
    uint32_t height;

    uint8_t bpp;
    uint8_t type;

    uint8_t red_position;
    uint8_t red_mask_size;

    uint8_t green_position;
    uint8_t green_mask_size;

    uint8_t blue_position;
    uint8_t blue_mask_size;

} framebuffer_boot_info_t;

bool framebuffer_read_multiboot(
    uint32_t multiboot_info_address,
    framebuffer_boot_info_t *info);

bool framebuffer_initialize(
    const framebuffer_boot_info_t *info);

bool framebuffer_is_available(void);

/*
 * Expand the permanent framebuffer virtual mapping.
 *
 * This is used by the runtime display driver so modes larger than the
 * boot mode can use the same mapped VGA VRAM.
 */
bool framebuffer_map_capacity(
    size_t capacity_bytes);

/*
 * Update geometry after the hardware display mode changes.
 *
 * Does not remap the framebuffer.
 */
bool framebuffer_set_geometry(
    uint32_t width,
    uint32_t height,
    uint32_t pitch,
    uint32_t bpp);

uint32_t framebuffer_get_width(void);
uint32_t framebuffer_get_height(void);
uint32_t framebuffer_get_pitch(void);
uint32_t framebuffer_get_bpp(void);

uintptr_t framebuffer_get_physical_address(void);

size_t framebuffer_get_mapped_capacity(void);

void framebuffer_put_pixel(
    uint32_t x,
    uint32_t y,
    uint32_t rgb);

void framebuffer_clear(
    uint32_t rgb);

void framebuffer_fill_rect(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t rgb);

void framebuffer_draw_string(
    uint32_t x,
    uint32_t y,
    const char *text,
    uint32_t rgb,
    uint32_t scale);

void framebuffer_blit_rgb32(
    uint32_t destination_x,
    uint32_t destination_y,
    uint32_t width,
    uint32_t height,
    const uint32_t *source_pixels,
    uint32_t source_pitch);

#endif