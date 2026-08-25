#ifndef KERNEL_GUI_WINDOW_H
#define KERNEL_GUI_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/gui/surface.h>

/*
 * ------------------------------------------------------------
 * GUI z-order classes
 * ------------------------------------------------------------
 *
 * These define broad stacking groups.
 *
 * Ordering within a class comes later when the window manager
 * owns multiple windows.
 */
typedef enum gui_z_class
{
    GUI_Z_DESKTOP = 0,
    GUI_Z_NORMAL,
    GUI_Z_TOPMOST,
    GUI_Z_TASKBAR,
    GUI_Z_POPUP,
    GUI_Z_OVERLAY,
    GUI_Z_CURSOR
} gui_z_class_t;

/*
 * ------------------------------------------------------------
 * GUI window
 * ------------------------------------------------------------
 *
 * For the first G1 milestone a window owns:
 *
 *     - screen position
 *     - an off-screen surface
 *     - visibility
 *     - z-order class
 *
 * There is deliberately no input/focus/widget state yet.
 */
typedef struct gui_window
{
    int32_t x;
    int32_t y;

    gui_z_class_t z_class;

    bool visible;

    gui_surface_t surface;
} gui_window_t;

bool gui_window_create(
    gui_window_t *window,
    int32_t x,
    int32_t y,
    uint32_t width,
    uint32_t height,
    gui_z_class_t z_class);

void gui_window_destroy(
    gui_window_t *window);

gui_surface_t *gui_window_surface(
    gui_window_t *window);

gui_rect_t gui_window_bounds(
    const gui_window_t *window);

void gui_window_set_visible(
    gui_window_t *window,
    bool visible);

/*
 * Composite this window into the compositor backbuffer.
 *
 * The window is clipped against the physical screen bounds.
 *
 * This function marks the affected compositor region dirty but does
 * not call gui_compositor_present().
 */
void gui_window_composite(
    const gui_window_t *window);

#endif