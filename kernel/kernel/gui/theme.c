#include <kernel/gui/theme.h>


static const gui_theme_t default_theme =
{
    /*
     * Desktop.
     */
    .desktop_gradient_top =
        GUI_RGB(24u, 49u, 83u),

    .desktop_gradient_bottom =
        GUI_RGB(10u, 23u, 42u),

    /*
     * Window material.
     */
    .window_gradient_top =
        GUI_RGBA(250u, 252u, 255u, 242u),

    .window_gradient_bottom =
        GUI_RGBA(226u, 232u, 241u, 235u),

    .window_border =
        GUI_RGBA(255u, 255u, 255u, 118u),

    /*
     * Keep the shadow relatively restrained.
     * Multiple painter falloff rings provide softness.
     */
    .window_shadow =
        GUI_RGBA(0u, 0u, 0u, 68u),

    /*
     * Typography.
     */
    .text_primary =
        GUI_RGB(24u, 31u, 42u),

    .text_secondary =
        GUI_RGB(80u, 91u, 108u),

    /*
     * System accent.
     */
    .accent =
        GUI_RGB(65u, 139u, 255u),

    /*
     * Window geometry.
     */
    .window_corner_radius =
        18u,

    .window_border_thickness =
        1u,

    .window_shadow_offset_x =
        0,

    .window_shadow_offset_y =
        8,

    .window_shadow_spread =
        1u,

    .window_shadow_blur =
        12u
};


const gui_theme_t *gui_theme_default(void)
{
    return
        &default_theme;
}