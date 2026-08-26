#ifndef KERNEL_GUI_THEME_H
#define KERNEL_GUI_THEME_H

#include <stdint.h>

#include <kernel/gui/surface.h>


typedef struct gui_theme
{
    /*
     * Desktop.
     */
    gui_color_t desktop_gradient_top;
    gui_color_t desktop_gradient_bottom;

    /*
     * Normal windows.
     */
    gui_color_t window_gradient_top;
    gui_color_t window_gradient_bottom;

    gui_color_t window_border;
    gui_color_t window_shadow;

    /*
     * Top system bar.
     */
    gui_color_t topbar_background;
    gui_color_t topbar_border;

    gui_color_t topbar_text;
    gui_color_t topbar_text_secondary;

    /*
     * Floating bottom dock.
     */
    gui_color_t taskbar_gradient_top;
    gui_color_t taskbar_gradient_bottom;

    gui_color_t taskbar_border;
    gui_color_t taskbar_shadow;

    gui_color_t taskbar_text;

    /*
     * Typography.
     */
    gui_color_t text_primary;
    gui_color_t text_secondary;

    /*
     * System accent.
     */
    gui_color_t accent;

    /*
     * Normal window geometry.
     */
    uint32_t window_corner_radius;
    uint32_t window_border_thickness;

    int32_t window_shadow_offset_x;
    int32_t window_shadow_offset_y;

    uint32_t window_shadow_spread;
    uint32_t window_shadow_blur;

    /*
     * Top system bar geometry.
     */
    uint32_t topbar_height;

    /*
     * Floating dock geometry.
     */
    uint32_t taskbar_corner_radius;
    uint32_t taskbar_border_thickness;

    int32_t taskbar_shadow_offset_x;
    int32_t taskbar_shadow_offset_y;

    uint32_t taskbar_shadow_spread;
    uint32_t taskbar_shadow_blur;

} gui_theme_t;


const gui_theme_t *gui_theme_default(void);


#endif