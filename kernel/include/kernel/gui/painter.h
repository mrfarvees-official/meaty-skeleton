#ifndef KERNEL_GUI_PAINTER_H
#define KERNEL_GUI_PAINTER_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/gui/surface.h>


/*
 * Higher-level GUI drawing operations.
 *
 * surface.c remains the low-level pixel/rectangle layer.
 *
 * painter.c provides reusable visual primitives for windows,
 * widgets, menus, taskbars and desktop UI.
 */


void gui_painter_fill_rounded_vertical_gradient(
    gui_surface_t *surface,
    gui_rect_t rect,
    uint32_t radius,
    gui_color_t top_color,
    gui_color_t bottom_color);


void gui_painter_fill_rounded_vertical_gradient_blend(
    gui_surface_t *surface,
    gui_rect_t rect,
    uint32_t radius,
    gui_color_t top_color,
    gui_color_t bottom_color);


/*
 * Draw a rounded rectangle border.
 *
 * thickness is measured inward from rect.
 *
 * The border is source-over blended into the destination.
 */
void gui_painter_stroke_rounded_rect(
    gui_surface_t *surface,
    gui_rect_t rect,
    uint32_t radius,
    uint32_t thickness,
    gui_color_t color);


/*
 * Draw a soft rounded shadow.
 *
 * rect describes the object casting the shadow.
 *
 * offset_x / offset_y:
 *     shadow displacement
 *
 * spread:
 *     solid expansion around the object before blur
 *
 * blur_radius:
 *     number of soft falloff pixels
 *
 * The shadow is always blended into the destination.
 */
void gui_painter_draw_rounded_shadow(
    gui_surface_t *surface,
    gui_rect_t rect,
    uint32_t radius,
    int32_t offset_x,
    int32_t offset_y,
    uint32_t spread,
    uint32_t blur_radius,
    gui_color_t color);


#endif