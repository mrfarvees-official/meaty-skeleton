#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/painter.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/taskbar.h>
#include <kernel/gui/theme.h>
#include <kernel/gui/window.h>


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

#define GUI_TASKBAR_SHADOW_PADDING_X \
    16u

#define GUI_TASKBAR_SHADOW_PADDING_TOP \
    12u

#define GUI_TASKBAR_SHADOW_PADDING_BOTTOM \
    18u


static bool
    taskbar_initialized;

static gui_window_t
    taskbar_window;


/*
 * ------------------------------------------------------------
 * Rendering
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

    gui_surface_clear(
        surface,
        GUI_TRANSPARENT);

    gui_rect_t panel;

    panel.x =
        GUI_TASKBAR_SHADOW_PADDING_X;

    panel.y =
        GUI_TASKBAR_SHADOW_PADDING_TOP;

    panel.width =
        surface->width -
        GUI_TASKBAR_SHADOW_PADDING_X * 2u;

    panel.height =
        GUI_TASKBAR_PANEL_HEIGHT;

    gui_painter_draw_rounded_shadow(
        surface,
        panel,
        theme->taskbar_corner_radius,
        theme->taskbar_shadow_offset_x,
        theme->taskbar_shadow_offset_y,
        theme->taskbar_shadow_spread,
        theme->taskbar_shadow_blur,
        theme->taskbar_shadow);

    gui_painter_fill_rounded_vertical_gradient(
        surface,
        panel,
        theme->taskbar_corner_radius,
        theme->taskbar_gradient_top,
        theme->taskbar_gradient_bottom);

    gui_painter_stroke_rounded_rect(
        surface,
        panel,
        theme->taskbar_corner_radius,
        theme->taskbar_border_thickness,
        theme->taskbar_border);

    /*
     * Deliberately empty.
     *
     * This dock is reserved for running/pinned applications.
     */
    return true;
}


/*
 * ------------------------------------------------------------
 * Initialization
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

    int32_t window_x =
        (int32_t)
        (panel_x -
         GUI_TASKBAR_SHADOW_PADDING_X);

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