#include <kernel/gui/theme.h>


static const gui_theme_t default_theme =
{
    /*
     * Deep blue desktop gradient.
     */
    .desktop_gradient_top =
        GUI_RGB(24u, 49u, 83u),

    .desktop_gradient_bottom =
        GUI_RGB(10u, 23u, 42u),

    /*
     * Slightly translucent neutral window surface.
     *
     * The compositor will blend this against whatever is behind
     * the window.
     */
    .window_gradient_top =
        GUI_RGBA(250u, 252u, 255u, 242u),

    .window_gradient_bottom =
        GUI_RGBA(226u, 232u, 241u, 235u),

    .window_border =
        GUI_RGBA(255u, 255u, 255u, 110u),

    .window_shadow =
        GUI_RGBA(0u, 0u, 0u, 72u),

    .text_primary =
        GUI_RGB(24u, 31u, 42u),

    .text_secondary =
        GUI_RGB(80u, 91u, 108u),

    .accent =
        GUI_RGB(65u, 139u, 255u),

    .window_corner_radius =
        18u
};


const gui_theme_t *gui_theme_default(void)
{
    return
        &default_theme;
}