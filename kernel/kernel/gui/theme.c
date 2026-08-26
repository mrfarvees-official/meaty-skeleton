#include <kernel/gui/theme.h>


static const gui_theme_t default_theme =
{
    /*
     * --------------------------------------------------------
     * Desktop fallback
     * --------------------------------------------------------
     *
     * Normally covered by wallpaper.
     */
    .desktop_gradient_top =
        GUI_RGB(226u, 233u, 242u),

    .desktop_gradient_bottom =
        GUI_RGB(194u, 205u, 219u),


    /*
     * --------------------------------------------------------
     * Normal windows
     * --------------------------------------------------------
     */
    .window_gradient_top =
        GUI_RGBA(255u, 255u, 255u, 246u),

    .window_gradient_bottom =
        GUI_RGBA(235u, 239u, 245u, 240u),

    .window_border =
        GUI_RGBA(255u, 255u, 255u, 210u),

    .window_shadow =
        GUI_RGBA(0u, 0u, 0u, 54u),


    /*
     * --------------------------------------------------------
     * Top system bar
     * --------------------------------------------------------
     */
    .topbar_background =
        GUI_RGBA(250u, 252u, 255u, 232u),

    .topbar_border =
        GUI_RGBA(80u, 90u, 105u, 32u),

    .topbar_text =
        GUI_RGB(29u, 34u, 43u),

    .topbar_text_secondary =
        GUI_RGB(92u, 101u, 115u),


    /*
     * --------------------------------------------------------
     * Floating application dock
     * --------------------------------------------------------
     */
    .taskbar_gradient_top =
        GUI_RGBA(255u, 255u, 255u, 232u),

    .taskbar_gradient_bottom =
        GUI_RGBA(231u, 236u, 243u, 224u),

    .taskbar_border =
        GUI_RGBA(255u, 255u, 255u, 190u),

    .taskbar_shadow =
        GUI_RGBA(0u, 0u, 0u, 68u),

    .taskbar_text =
        GUI_RGB(30u, 35u, 44u),


    /*
     * --------------------------------------------------------
     * Typography
     * --------------------------------------------------------
     */
    .text_primary =
        GUI_RGB(28u, 33u, 42u),

    .text_secondary =
        GUI_RGB(92u, 101u, 115u),


    /*
     * --------------------------------------------------------
     * Accent
     * --------------------------------------------------------
     */
    .accent =
        GUI_RGB(54u, 123u, 246u),


    /*
     * --------------------------------------------------------
     * Window geometry
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
     * Topbar
     * --------------------------------------------------------
     */
    .topbar_height =
        32u,


    /*
     * --------------------------------------------------------
     * Floating application dock
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