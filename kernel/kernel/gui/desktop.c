#include <stdbool.h>
#include <stddef.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/desktop.h>
#include <kernel/gui/font.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/window.h>


/*
 * ------------------------------------------------------------
 * First desktop scene
 * ------------------------------------------------------------
 */

#define GUI_DESKTOP_BACKGROUND_COLOR \
    GUI_RGB(32u, 96u, 160u)


/*
 * ------------------------------------------------------------
 * Temporary G1 validation window
 * ------------------------------------------------------------
 */

#define GUI_TEST_WINDOW_WIDTH  420u
#define GUI_TEST_WINDOW_HEIGHT 260u

#define GUI_TEST_WINDOW_COLOR \
    GUI_RGB(230u, 230u, 230u)

#define GUI_TEST_TEXT_COLOR \
    GUI_RGB(24u, 24u, 24u)

#define GUI_TEST_TEXT_PIXEL_HEIGHT \
    28u


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

    gui_surface_t *window_surface =
        gui_window_surface(
            &desktop_test_window);

    if (window_surface == NULL)
    {
        gui_window_destroy(
            &desktop_test_window);

        return false;
    }

    gui_surface_clear(
        window_surface,
        GUI_TEST_WINDOW_COLOR);

    /*
     * Font rendering happens exclusively in the window's
     * off-screen gui_surface_t.
     *
     * No font code knows anything about the framebuffer.
     */
    if (!gui_font_draw_text(
            window_surface,
            font,
            28,
            28,
            GUI_TEST_TEXT_PIXEL_HEIGHT,
            "Meaty OS",
            GUI_TEST_TEXT_COLOR))
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

    /*
     * EXT2 is already mounted and installed as the VFS root before
     * GUI bootstrap reaches this point.
     *
     * Load the configured system font once.
     */
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

    /*
     * Rebuild the complete scene from back to front.
     *
     * Current scene:
     *
     *     desktop
     *     one GUI_Z_NORMAL test window
     *
     * The text has already been rasterized into the window's
     * owned surface.
     */
    gui_surface_clear(
        surface,
        GUI_DESKTOP_BACKGROUND_COLOR);

    gui_window_composite(
        &desktop_test_window);

    gui_compositor_damage_all();

    gui_compositor_present();
}