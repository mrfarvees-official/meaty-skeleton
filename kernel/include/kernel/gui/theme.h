#ifndef KERNEL_GUI_THEME_H
#define KERNEL_GUI_THEME_H

#include <stdint.h>

#include <kernel/gui/surface.h>


typedef struct gui_theme
{
    gui_color_t desktop_gradient_top;
    gui_color_t desktop_gradient_bottom;

    gui_color_t window_gradient_top;
    gui_color_t window_gradient_bottom;

    gui_color_t window_border;

    gui_color_t window_shadow;

    gui_color_t text_primary;
    gui_color_t text_secondary;

    gui_color_t accent;

    uint32_t window_corner_radius;
} gui_theme_t;


/*
 * Built-in system theme.
 *
 * This is deliberately generic. Widgets can consume the same
 * theme object later without knowing anything about the desktop.
 */
const gui_theme_t *gui_theme_default(void);


#endif