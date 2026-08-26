#ifndef KERNEL_GUI_WINDOW_H
#define KERNEL_GUI_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/gui/surface.h>


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

void gui_window_composite(
    const gui_window_t *window);


/*
 * ------------------------------------------------------------
 * First normal-window registry / focus model
 * ------------------------------------------------------------
 *
 * Only GUI_Z_NORMAL windows participate for now.
 *
 * Other z classes remain explicitly composited by the desktop shell.
 */

gui_window_t *gui_window_active(void);


/*
 * Focus a visible normal window.
 *
 * Focusing also moves it to the front of the normal-window stack.
 */
bool gui_window_focus(
    gui_window_t *window);


/*
 * Focus the uppermost normal window containing the screen point.
 */
bool gui_window_focus_at_point(
    int32_t x,
    int32_t y);


/*
 * Keyboard traversal.
 */
bool gui_window_focus_next(void);

bool gui_window_focus_previous(void);


/*
 * Composite all visible normal windows from back to front.
 */
void gui_window_composite_normal_windows(void);


#endif