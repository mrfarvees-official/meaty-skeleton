#include <stdbool.h>
#include <stddef.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/desktop.h>
#include <kernel/gui/font.h>
#include <kernel/gui/painter.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/theme.h>
#include <kernel/gui/window.h>


/*
 * ------------------------------------------------------------
 * Temporary theme/painter validation window
 * ------------------------------------------------------------
 */

#define GUI_TEST_WINDOW_WIDTH \
    520u

#define GUI_TEST_WINDOW_HEIGHT \
    340u

/*
 * Window surface includes transparent padding around the visual
 * panel so the shadow remains part of the window's composited
 * surface.
 */
#define GUI_TEST_WINDOW_MARGIN \
    28u

#define GUI_TEST_TEXT_PIXEL_HEIGHT \
    28u

#define GUI_TEST_SUBTEXT_PIXEL_HEIGHT \
    16u


static bool desktop_initialized;

static gui_window_t
    desktop_test_window;

static bool
    desktop_test_window_initialized;


static bool gui_desktop_create_test_window(void)
{
    if (desktop_test_window_initialized)
        return true;

    gui_surface_t *screen =
        gui_compositor_surface();

    if (screen == NULL ||
        screen->pixels == NULL)
    {
        return false;
    }

    gui_font_t *font =
        gui_font_default();

    if (font == NULL)
        return false;

    const gui_theme_t *theme =
        gui_theme_default();

    if (theme == NULL)
        return false;

    int32_t x =
        (int32_t)
        (screen->width / 2u) -
        (int32_t)
        (GUI_TEST_WINDOW_WIDTH / 2u);

    int32_t y =
        (int32_t)
        (screen->height / 2u) -
        (int32_t)
        (GUI_TEST_WINDOW_HEIGHT / 2u);

    if (!gui_window_create(
            &desktop_test_window,
            x,
            y,
            GUI_TEST_WINDOW_WIDTH,
            GUI_TEST_WINDOW_HEIGHT,
            GUI_Z_NORMAL))
    {
        return false;
    }

    gui_surface_t *surface =
        gui_window_surface(
            &desktop_test_window);

    if (surface == NULL ||
        surface->pixels == NULL)
    {
        gui_window_destroy(
            &desktop_test_window);

        return false;
    }

    /*
     * Everything outside the visual panel, including its rounded
     * corners, remains transparent.
     */
    gui_surface_clear(
        surface,
        GUI_TRANSPARENT);

    gui_rect_t panel_rect;

    panel_rect.x =
        GUI_TEST_WINDOW_MARGIN;

    panel_rect.y =
        GUI_TEST_WINDOW_MARGIN;

    panel_rect.width =
        GUI_TEST_WINDOW_WIDTH -
        GUI_TEST_WINDOW_MARGIN * 2u;

    panel_rect.height =
        GUI_TEST_WINDOW_HEIGHT -
        GUI_TEST_WINDOW_MARGIN * 2u;

    /*
     * Shadow.
     */
    gui_painter_draw_rounded_shadow(
        surface,
        panel_rect,
        theme->window_corner_radius,
        theme->window_shadow_offset_x,
        theme->window_shadow_offset_y,
        theme->window_shadow_spread,
        theme->window_shadow_blur,
        theme->window_shadow);

    /*
     * Window material.
     *
     * The alpha in the gradient is retained in the window surface
     * and later composited against the desktop by window.c.
     */
    gui_painter_fill_rounded_vertical_gradient(
        surface,
        panel_rect,
        theme->window_corner_radius,
        theme->window_gradient_top,
        theme->window_gradient_bottom);

    /*
     * Real border stroke.
     *
     * Unlike the previous validation implementation this does not
     * tint the entire interior of the panel.
     */
    gui_painter_stroke_rounded_rect(
        surface,
        panel_rect,
        theme->window_corner_radius,
        theme->window_border_thickness,
        theme->window_border);

    /*
     * Existing TrueType renderer remains the text owner.
     */
    if (!gui_font_draw_text(
            surface,
            font,
            panel_rect.x + 34,
            panel_rect.y + 34,
            GUI_TEST_TEXT_PIXEL_HEIGHT,
            "Meaty OS",
            theme->text_primary))
    {
        gui_window_destroy(
            &desktop_test_window);

        return false;
    }

    /*
     * A second font size also gives us a useful visual check that
     * the font cache remains correct across multiple pixel sizes.
     */
    if (!gui_font_draw_text(
            surface,
            font,
            panel_rect.x + 35,
            panel_rect.y + 82,
            GUI_TEST_SUBTEXT_PIXEL_HEIGHT,
            "Modern GUI rendering foundation",
            theme->text_secondary))
    {
        gui_window_destroy(
            &desktop_test_window);

        return false;
    }

    desktop_test_window_initialized =
        true;

    return true;
}


bool gui_desktop_initialize(void)
{
    if (desktop_initialized)
        return true;

    if (!gui_compositor_is_initialized())
        return false;

    gui_surface_t *surface =
        gui_compositor_surface();

    if (surface == NULL ||
        surface->pixels == NULL ||
        surface->width == 0u ||
        surface->height == 0u)
    {
        return false;
    }

    if (!gui_font_system_initialize())
        return false;

    if (!gui_desktop_create_test_window())
        return false;

    desktop_initialized =
        true;

    gui_desktop_render();

    return true;
}


void gui_desktop_render(void)
{
    if (!desktop_initialized)
        return;

    gui_surface_t *surface =
        gui_compositor_surface();

    if (surface == NULL ||
        surface->pixels == NULL)
    {
        return;
    }

    const gui_theme_t *theme =
        gui_theme_default();

    if (theme == NULL)
        return;

    gui_rect_t desktop_rect;

    desktop_rect.x = 0;
    desktop_rect.y = 0;

    desktop_rect.width =
        surface->width;

    desktop_rect.height =
        surface->height;

    /*
     * Always keep the base scene opaque.
     */
    gui_surface_fill_vertical_gradient(
        surface,
        desktop_rect,
        theme->desktop_gradient_top,
        theme->desktop_gradient_bottom);

    gui_window_composite(
        &desktop_test_window);

    gui_compositor_damage_all();

    gui_compositor_present();
}