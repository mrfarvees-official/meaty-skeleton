#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/font.h>
#include <kernel/gui/painter.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/taskbar.h>
#include <kernel/gui/theme.h>
#include <kernel/gui/window.h>


/*
 * ------------------------------------------------------------
 * Dock layout
 * ------------------------------------------------------------
 *
 * Visible dock:
 *
 *     12.5% screen margin
 *     75% screen width
 *     12.5% screen margin
 *
 * Height is based on the future icon size rather than being an
 * arbitrary taskbar height.
 */

#define GUI_TASKBAR_WIDTH_PERCENT \
    75u

#define GUI_TASKBAR_ICON_SIZE \
    48u

#define GUI_TASKBAR_ICON_PADDING_Y \
    8u

#define GUI_TASKBAR_PANEL_HEIGHT \
    (GUI_TASKBAR_ICON_SIZE + \
     GUI_TASKBAR_ICON_PADDING_Y * 2u)

#define GUI_TASKBAR_BOTTOM_MARGIN \
    12u


/*
 * Transparent space around the visible dock.
 *
 * This is NOT part of the visible 75% dock width.
 *
 * It only gives the shadow enough room to render without being
 * clipped by the taskbar window surface.
 */
#define GUI_TASKBAR_SHADOW_PADDING_X \
    16u

#define GUI_TASKBAR_SHADOW_PADDING_TOP \
    12u

#define GUI_TASKBAR_SHADOW_PADDING_BOTTOM \
    18u


#define GUI_TASKBAR_FONT_PIXEL_HEIGHT \
    17u

#define GUI_TASKBAR_TEXT_PADDING_X \
    22


static bool taskbar_initialized;

static gui_window_t taskbar_window;


/*
 * ------------------------------------------------------------
 * Taskbar rendering
 * ------------------------------------------------------------
 */

static bool gui_taskbar_render_surface(void)
{
    gui_surface_t *surface =
        gui_window_surface(
            &taskbar_window);

    if (surface == NULL ||
        surface->pixels == NULL)
    {
        return false;
    }

    const gui_theme_t *theme =
        gui_theme_default();

    if (theme == NULL)
        return false;

    gui_font_t *font =
        gui_font_default();

    if (font == NULL)
        return false;

    /*
     * The taskbar window contains transparent padding around the
     * actual dock so its shadow can extend outside the material.
     */
    gui_surface_clear(
        surface,
        GUI_TRANSPARENT);

    gui_rect_t panel;

    panel.x =
        (int32_t)
        GUI_TASKBAR_SHADOW_PADDING_X;

    panel.y =
        (int32_t)
        GUI_TASKBAR_SHADOW_PADDING_TOP;

    panel.width =
        surface->width -
        GUI_TASKBAR_SHADOW_PADDING_X * 2u;

    panel.height =
        GUI_TASKBAR_PANEL_HEIGHT;

    /*
     * Floating dock shadow.
     */
    gui_painter_draw_rounded_shadow(
        surface,
        panel,
        theme->taskbar_corner_radius,
        theme->taskbar_shadow_offset_x,
        theme->taskbar_shadow_offset_y,
        theme->taskbar_shadow_spread,
        theme->taskbar_shadow_blur,
        theme->taskbar_shadow);

    /*
     * Translucent dock material.
     */
    gui_painter_fill_rounded_vertical_gradient(
        surface,
        panel,
        theme->taskbar_corner_radius,
        theme->taskbar_gradient_top,
        theme->taskbar_gradient_bottom);

    /*
     * Fine outer edge.
     */
    gui_painter_stroke_rounded_rect(
        surface,
        panel,
        theme->taskbar_corner_radius,
        theme->taskbar_border_thickness,
        theme->taskbar_border);

    /*
     * Temporary validation label only.
     *
     * This is not an interactive launcher yet.
     */
    // if (!gui_font_draw_text(
    //         surface,
    //         font,
    //         panel.x +
    //             GUI_TASKBAR_TEXT_PADDING_X,
    //         panel.y +
    //             (int32_t)
    //             ((GUI_TASKBAR_PANEL_HEIGHT -
    //               GUI_TASKBAR_FONT_PIXEL_HEIGHT) /
    //              2u),
    //         GUI_TASKBAR_FONT_PIXEL_HEIGHT,
    //         "Meaty OS",
    //         theme->taskbar_text))
    // {
    //     return false;
    // }

    return true;
}


/*
 * ------------------------------------------------------------
 * Public taskbar API
 * ------------------------------------------------------------
 */

bool gui_taskbar_initialize(void)
{
    if (taskbar_initialized)
        return true;

    if (!gui_compositor_is_initialized())
        return false;

    gui_surface_t *screen =
        gui_compositor_surface();

    if (screen == NULL ||
        screen->pixels == NULL ||
        screen->width == 0u ||
        screen->height == 0u)
    {
        return false;
    }

    /*
     * Visible dock width is exactly 75% of the physical screen.
     *
     * At 1360 px:
     *
     *     1360 * 75 / 100 = 1020
     *
     * Remaining:
     *
     *     1360 - 1020 = 340
     *
     * Therefore:
     *
     *     170 px left
     *     170 px right
     *
     * which is exactly 12.5% on either side.
     */
    uint32_t panel_width =
        (uint32_t)
        (((uint64_t)screen->width *
          GUI_TASKBAR_WIDTH_PERCENT) /
         100u);

    if (panel_width == 0u ||
        panel_width > screen->width)
    {
        return false;
    }

    uint32_t panel_x =
        (screen->width -
         panel_width) /
        2u;

    /*
     * The taskbar gui_window_t must be wider than the actual panel
     * because it owns transparent shadow padding.
     */
    uint32_t surface_width =
        panel_width +
        GUI_TASKBAR_SHADOW_PADDING_X * 2u;

    uint32_t surface_height =
        GUI_TASKBAR_SHADOW_PADDING_TOP +
        GUI_TASKBAR_PANEL_HEIGHT +
        GUI_TASKBAR_SHADOW_PADDING_BOTTOM;

    if (panel_x <
        GUI_TASKBAR_SHADOW_PADDING_X)
    {
        return false;
    }

    if (surface_height +
        GUI_TASKBAR_BOTTOM_MARGIN >
        screen->height)
    {
        return false;
    }

    /*
     * Position the window so that:
     *
     *     window.x + shadow_padding_x == panel_x
     *
     * Consequently the VISIBLE dock remains exactly centered and
     * exactly 75% wide.
     */
    int32_t window_x =
        (int32_t)
        (panel_x -
         GUI_TASKBAR_SHADOW_PADDING_X);

    /*
     * Position based on the bottom of the VISIBLE panel rather than
     * the bottom of the backing surface.
     *
     * Desired:
     *
     *     panel bottom
     *     12 px empty desktop
     *     screen bottom
     */
    uint32_t visible_panel_y =
        screen->height -
        GUI_TASKBAR_BOTTOM_MARGIN -
        GUI_TASKBAR_PANEL_HEIGHT;

    if (visible_panel_y <
        GUI_TASKBAR_SHADOW_PADDING_TOP)
    {
        return false;
    }

    int32_t window_y =
        (int32_t)
        (visible_panel_y -
         GUI_TASKBAR_SHADOW_PADDING_TOP);

    if (!gui_window_create(
            &taskbar_window,
            window_x,
            window_y,
            surface_width,
            surface_height,
            GUI_Z_TASKBAR))
    {
        return false;
    }

    if (!gui_taskbar_render_surface())
    {
        gui_window_destroy(
            &taskbar_window);

        return false;
    }

    taskbar_initialized =
        true;

    return true;
}


void gui_taskbar_composite(void)
{
    if (!taskbar_initialized)
        return;

    gui_window_composite(
        &taskbar_window);
}