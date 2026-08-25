#ifndef KERNEL_GUI_IMAGE_H
#define KERNEL_GUI_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/gui/surface.h>


typedef struct gui_image gui_image_t;


/*
 * ------------------------------------------------------------
 * Image loading/cache
 * ------------------------------------------------------------
 *
 * gui_image_get() returns a shared cached image.
 *
 * First successful request:
 *
 *     VFS file
 *       -> PNG decoder
 *       -> RGBA gui_surface_t
 *       -> cache
 *
 * Later requests for the same path reuse the decoded image.
 */
bool gui_image_get(
    const char *path,
    const gui_image_t **result);


/*
 * ------------------------------------------------------------
 * Image information
 * ------------------------------------------------------------
 */

uint32_t gui_image_width(
    const gui_image_t *image);

uint32_t gui_image_height(
    const gui_image_t *image);


/*
 * ------------------------------------------------------------
 * Native-size rendering
 * ------------------------------------------------------------
 *
 * Draw the complete image at its native dimensions.
 *
 * PNG alpha is source-over blended into destination.
 *
 * Drawing is clipped against destination bounds.
 */
void gui_image_draw(
    gui_surface_t *destination,
    const gui_image_t *image,
    int32_t x,
    int32_t y);


/*
 * ------------------------------------------------------------
 * Scaled rendering
 * ------------------------------------------------------------
 *
 * Draw the complete source image scaled into destination_rect.
 *
 * Nearest-neighbor sampling is intentionally used for this first
 * scaling milestone:
 *
 *     - simple
 *     - deterministic
 *     - no temporary scaling buffer
 *     - suitable for wallpaper validation and UI icons
 *
 * destination_rect may extend outside destination. Rendering is
 * clipped safely.
 *
 * PNG alpha remains source-over composited.
 */
void gui_image_draw_scaled(
    gui_surface_t *destination,
    const gui_image_t *image,
    gui_rect_t destination_rect);


#endif