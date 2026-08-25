#include <stdbool.h>
#include <stddef.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/desktop.h>
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
 * Temporary G1a validation window
 * ------------------------------------------------------------
 *
 * This is intentionally not a real application window yet.
 *
 * Its only purpose is to validate:
 *
 *     window-owned surfaces
 *     screen position
 *     composition
 *     clipping
 *     compositor damage
 *
 * It will disappear once the real window manager owns windows.
 */
#define GUI_TEST_WINDOW_WIDTH  420u
#define GUI_TEST_WINDOW_HEIGHT 260u

#define GUI_TEST_WINDOW_COLOR \
    GUI_RGB(230u, 230u, 230u)


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

    /*
     * Deliberately place part of the test window near the center.
     *
     * Position calculations remain signed so future windows can
     * legally exist partly outside the screen and rely on clipping.
     */
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
     * G1a contains only:
     *
     *     desktop
     *     one GUI_Z_NORMAL test window
     */
    gui_surface_clear(
        surface,
        GUI_DESKTOP_BACKGROUND_COLOR);

    gui_window_composite(
        &desktop_test_window);

    /*
     * Because the desktop background itself was completely rebuilt,
     * this first scene render is full-screen damage.
     *
     * The window's own damage call remains correct and becomes useful
     * once we stop rebuilding the complete desktop every frame.
     */
    gui_compositor_damage_all();

    gui_compositor_present();
}