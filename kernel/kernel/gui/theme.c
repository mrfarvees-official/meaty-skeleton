#include <kernel/gui/theme.h>


static const gui_theme_t default_theme =
{
    /*
     * --------------------------------------------------------
     * Desktop
     * --------------------------------------------------------
     */

    .desktop_gradient_top =
        GUI_RGB(24u, 49u, 83u),

    .desktop_gradient_bottom =
        GUI_RGB(10u, 23u, 42u),

    /*
     * --------------------------------------------------------
     * Normal window material
     * --------------------------------------------------------
     */

    .window_gradient_top =
        GUI_RGBA(250u, 252u, 255u, 242u),

    .window_gradient_bottom =
        GUI_RGBA(226u, 232u, 241u, 235u),

    .window_border =
        GUI_RGBA(255u, 255u, 255u, 118u),

    .window_shadow =
        GUI_RGBA(0u, 0u, 0u, 68u),

    /*
     * --------------------------------------------------------
     * Top system bar
     * --------------------------------------------------------
     */

    .topbar_background =
        GUI_RGBA(16u, 23u, 34u, 220u),

    .topbar_border =
        GUI_RGBA(255u, 255u, 255u, 32u),

    .topbar_text =
        GUI_RGB(245u, 247u, 250u),

    .topbar_text_secondary =
        GUI_RGB(216u, 222u, 231u),

    /*
     * --------------------------------------------------------
     * Floating dock
     * --------------------------------------------------------
     */

    .taskbar_gradient_top =
        GUI_RGBA(38u, 47u, 63u, 232u),

    .taskbar_gradient_bottom =
        GUI_RGBA(25u, 32u, 45u, 224u),

    .taskbar_border =
        GUI_RGBA(255u, 255u, 255u, 72u),

    .taskbar_shadow =
        GUI_RGBA(0u, 0u, 0u, 82u),

    .taskbar_text =
        GUI_RGB(242u, 245u, 250u),

    /*
     * --------------------------------------------------------
     * Typography
     * --------------------------------------------------------
     */

    .text_primary =
        GUI_RGB(24u, 31u, 42u),

    .text_secondary =
        GUI_RGB(80u, 91u, 108u),

    /*
     * --------------------------------------------------------
     * Accent
     * --------------------------------------------------------
     */

    .accent =
        GUI_RGB(65u, 139u, 255u),

    /*
     * --------------------------------------------------------
     * Normal window geometry
     * --------------------------------------------------------
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
        12u,

    /*
     * --------------------------------------------------------
     * Top system bar geometry
     * --------------------------------------------------------
     */

    .topbar_height =
        32u,

    /*
     * --------------------------------------------------------
     * Floating dock geometry
     * --------------------------------------------------------
     */

    .taskbar_corner_radius =
        18u,

    .taskbar_border_thickness =
        1u,

    .taskbar_shadow_offset_x =
        0,

    .taskbar_shadow_offset_y =
        4,

    .taskbar_shadow_spread =
        1u,

    .taskbar_shadow_blur =
        8u
};


const gui_theme_t *gui_theme_default(void)
{
    return
        &default_theme;
}