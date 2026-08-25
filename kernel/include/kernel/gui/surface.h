#ifndef KERNEL_GUI_SURFACE_H
#define KERNEL_GUI_SURFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/*
 * GUI colors use straight alpha:
 *
 *     0xAARRGGBB
 *
 * This representation is independent of the physical framebuffer
 * channel layout.
 *
 * Existing GUI_RGB() callers remain fully opaque.
 */
typedef uint32_t gui_color_t;


#define GUI_RGBA(r, g, b, a) \
    ((((uint32_t)(a) & 0xFFu) << 24) | \
     (((uint32_t)(r) & 0xFFu) << 16) | \
     (((uint32_t)(g) & 0xFFu) << 8)  | \
     ((uint32_t)(b) & 0xFFu))

#define GUI_RGB(r, g, b) \
    GUI_RGBA((r), (g), (b), 255u)

#define GUI_TRANSPARENT \
    GUI_RGBA(0u, 0u, 0u, 0u)


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

    /*
     * Pitch in bytes.
     */
    uint32_t pitch;

    gui_color_t *pixels;

    /*
     * True when this surface allocated pixels through the
     * kernel heap and therefore owns them.
     */
    bool owns_pixels;
} gui_surface_t;


/*
 * ------------------------------------------------------------
 * Color helpers
 * ------------------------------------------------------------
 */

uint8_t gui_color_alpha(
    gui_color_t color);

uint8_t gui_color_red(
    gui_color_t color);

uint8_t gui_color_green(
    gui_color_t color);

uint8_t gui_color_blue(
    gui_color_t color);


/*
 * Straight-alpha source-over composition:
 *
 *     result = source OVER destination
 */
gui_color_t gui_color_blend(
    gui_color_t destination,
    gui_color_t source);


/*
 * ------------------------------------------------------------
 * Surface lifetime
 * ------------------------------------------------------------
 */

bool gui_surface_create(
    gui_surface_t *surface,
    uint32_t width,
    uint32_t height);

void gui_surface_destroy(
    gui_surface_t *surface);


/*
 * ------------------------------------------------------------
 * Basic drawing
 * ------------------------------------------------------------
 */

void gui_surface_clear(
    gui_surface_t *surface,
    gui_color_t color);

/*
 * Replace the destination pixel exactly.
 */
void gui_surface_put_pixel(
    gui_surface_t *surface,
    int32_t x,
    int32_t y,
    gui_color_t color);

/*
 * Source-over blend one pixel.
 */
void gui_surface_blend_pixel(
    gui_surface_t *surface,
    int32_t x,
    int32_t y,
    gui_color_t color);

/*
 * Replace every pixel in the rectangle exactly.
 */
void gui_surface_fill_rect(
    gui_surface_t *surface,
    gui_rect_t rect,
    gui_color_t color);

/*
 * Source-over blend a solid rectangle.
 */
void gui_surface_fill_rect_blend(
    gui_surface_t *surface,
    gui_rect_t rect,
    gui_color_t color);


/*
 * ------------------------------------------------------------
 * Theme rendering primitives
 * ------------------------------------------------------------
 */

/*
 * Vertical top-to-bottom gradient.
 *
 * Pixels are replaced rather than blended.
 */
void gui_surface_fill_vertical_gradient(
    gui_surface_t *surface,
    gui_rect_t rect,
    gui_color_t top_color,
    gui_color_t bottom_color);

/*
 * Vertical top-to-bottom gradient composited over the existing
 * surface.
 */
void gui_surface_fill_vertical_gradient_blend(
    gui_surface_t *surface,
    gui_rect_t rect,
    gui_color_t top_color,
    gui_color_t bottom_color);

/*
 * Rounded solid rectangle.
 *
 * radius is expressed in pixels and is automatically clamped.
 */
void gui_surface_fill_rounded_rect(
    gui_surface_t *surface,
    gui_rect_t rect,
    uint32_t radius,
    gui_color_t color);

/*
 * Rounded solid rectangle using source-over blending.
 */
void gui_surface_fill_rounded_rect_blend(
    gui_surface_t *surface,
    gui_rect_t rect,
    uint32_t radius,
    gui_color_t color);


/*
 * ------------------------------------------------------------
 * Rectangle helpers
 * ------------------------------------------------------------
 */

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