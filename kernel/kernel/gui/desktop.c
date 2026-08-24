#include <stdbool.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/desktop.h>
#include <kernel/gui/surface.h>
#include <kernel/logger.h>

/*
 * ------------------------------------------------------------
 * First desktop scene
 * ------------------------------------------------------------
 *
 * This is deliberately only the desktop background.
 *
 * No:
 *
 *     - windows
 *     - widgets
 *     - taskbar
 *     - icons
 *     - Explorer
 *     - input handling
 *
 * The desktop draws exclusively into the compositor's backbuffer.
 * Physical framebuffer ownership remains with the compositor.
 */

/*
 * Initial desktop background.
 *
 * Keep this here rather than in the compositor because the
 * compositor is presentation infrastructure and should not know
 * what a desktop looks like.
 */
#define GUI_DESKTOP_BACKGROUND_COLOR \
    GUI_RGB(32u, 96u, 160u)

static bool desktop_initialized;

bool gui_desktop_initialize(void)
{
    log_info(
        "desktop: initialize entered\n");

    if (desktop_initialized)
    {
        log_info(
            "desktop: already initialized\n");

        return true;
    }

    if (!gui_compositor_is_initialized())
    {
        log_error(
            "desktop: compositor not initialized\n");

        return false;
    }

    log_info(
        "desktop: compositor initialized\n");

    gui_surface_t *surface =
        gui_compositor_surface();

    if (surface == NULL)
    {
        log_error(
            "desktop: compositor surface is NULL\n");

        return false;
    }

    log_info(
        "desktop: surface=%p pixels=%p width=%u height=%u pitch=%u\n",
        (void *)surface,
        (void *)surface->pixels,
        (unsigned)surface->width,
        (unsigned)surface->height,
        (unsigned)surface->pitch);

    if (surface->pixels == NULL ||
        surface->width == 0u ||
        surface->height == 0u)
    {
        log_error(
            "desktop: invalid compositor surface\n");

        return false;
    }

    desktop_initialized =
        true;

    log_info(
        "desktop: calling render\n");

    gui_desktop_render();

    // log_info(
    //     "desktop: render returned\n");

    return true;
}

void gui_desktop_render(void)
{
    log_info(
        "desktop: render entered\n");

    if (!desktop_initialized)
    {
        log_error(
            "desktop: render before initialization\n");

        return;
    }

    gui_surface_t *surface =
        gui_compositor_surface();

    if (surface == NULL ||
        surface->pixels == NULL)
    {
        log_error(
            "desktop: render has invalid surface\n");

        return;
    }

    log_info(
        "desktop: clearing surface\n");

    gui_surface_clear(
        surface,
        GUI_DESKTOP_BACKGROUND_COLOR);

    log_info(
        "desktop: surface cleared\n");

    gui_compositor_damage_all();

    log_info(
        "desktop: full damage marked\n");

    gui_compositor_present();

    // log_info(
    //     "desktop: present returned\n");
}

