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
 * The first successful load of a path:
 *
 *     VFS file
 *       -> PNG decoder
 *       -> RGBA gui_surface_t
 *       -> image cache
 *
 * Later calls for the same path do not touch the filesystem.
 *
 * Cached images currently live for the lifetime of the GUI.
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
 * Rendering
 * ------------------------------------------------------------
 *
 * Draw the complete image at its native size.
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


#endif