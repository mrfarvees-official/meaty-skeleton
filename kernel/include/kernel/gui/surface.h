#ifndef KERNEL_GUI_SURFACE_H
#define KERNEL_GUI_SURFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t gui_color_t;

/*
 * GUI colors use:
 *
 *     0x00RRGGBB
 *
 * independent of the physical framebuffer channel layout.
 */
#define GUI_RGB(r, g, b) \
    ((((uint32_t)(r) & 0xFFu) << 16) | \
     (((uint32_t)(g) & 0xFFu) << 8)  | \
     ((uint32_t)(b) & 0xFFu))

typedef struct gui_rect
{
    int32_t x;
    int32_t y;

    uint32_t width;
    uint32_t height;
} gui_rect_t;

typedef struct gui_surface
{
    uint32_t width;
    uint32_t height;

    /**
     * Pitch in bytes
     */
    uint32_t pitch;

    gui_color_t *pixels;

    /**
     * True when this surface allocated pixels through
     * the kernel heap and therefore owns them.
     */
    bool owns_pixels;
} gui_surface_t;

bool gui_surface_create(
    gui_surface_t *surface,
    uint32_t width,
    uint32_t height);

void gui_surface_destroy(
    gui_surface_t *surface);

void gui_surface_clear(
    gui_surface_t *surface,
    gui_color_t color);

void gui_surface_put_pixel(
    gui_surface_t *surface,
    int32_t x,
    int32_t y,
    gui_color_t color);

void gui_surface_fill_rect(
    gui_surface_t *surface,
    gui_rect_t rect,
    gui_color_t color);

bool gui_rect_intersect(
    gui_rect_t first,
    gui_rect_t second,
    gui_rect_t *result);

bool gui_rect_union(
    gui_rect_t first,
    gui_rect_t second,
    gui_rect_t *result);

bool gui_rect_is_empty(
    gui_rect_t rect);

#endif