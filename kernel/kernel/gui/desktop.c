#include <stdbool.h>
#include <stddef.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/desktop.h>
#include <kernel/gui/font.h>
#include <kernel/gui/image.h>
#include <kernel/gui/painter.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/taskbar.h>
#include <kernel/gui/theme.h>
#include <kernel/gui/window.h>


/*
 * ------------------------------------------------------------
 * Desktop assets
 * ------------------------------------------------------------
 */

#define GUI_DESKTOP_WALLPAPER_PATH \
    "/wallpapers/default.png"

#define GUI_DESKTOP_VALIDATION_ICON_PATH \
    "/icons/apps/terminal.png"


/*
 * ------------------------------------------------------------
 * Temporary normal-window validation surface
 * ------------------------------------------------------------
 */

#define GUI_TEST_WINDOW_WIDTH \
    520u

#define GUI_TEST_WINDOW_HEIGHT \
    340u

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


/*
 * Cached filesystem-backed assets.
 *
 * gui_image_t objects themselves are owned by the global image
 * cache.
 */
static const gui_image_t *
    desktop_wallpaper;

static const gui_image_t *
    desktop_validation_icon;


/*
 * ------------------------------------------------------------
 * Temporary normal window
 * ------------------------------------------------------------
 */

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

    y -= 30;

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

    gui_painter_draw_rounded_shadow(
        surface,
        panel_rect,
        theme->window_corner_radius,
        theme->window_shadow_offset_x,
        theme->window_shadow_offset_y,
        theme->window_shadow_spread,
        theme->window_shadow_blur,
        theme->window_shadow);

    gui_painter_fill_rounded_vertical_gradient(
        surface,
        panel_rect,
        theme->window_corner_radius,
        theme->window_gradient_top,
        theme->window_gradient_bottom);

    gui_painter_stroke_rounded_rect(
        surface,
        panel_rect,
        theme->window_corner_radius,
        theme->window_border_thickness,
        theme->window_border);

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

    if (!gui_font_draw_text(
            surface,
            font,
            panel_rect.x + 35,
            panel_rect.y + 82,
            GUI_TEST_SUBTEXT_PIXEL_HEIGHT,
            "PNG image subsystem",
            theme->text_secondary))
    {
        gui_window_destroy(
            &desktop_test_window);

        return false;
    }

    /*
     * Alpha-validation image.
     *
     * The terminal icon should have transparent pixels around the
     * artwork. Those pixels must reveal the translucent window
     * underneath rather than a black/white square.
     */
    if (desktop_validation_icon != NULL)
    {
        gui_image_draw(
            surface,
            desktop_validation_icon,
            panel_rect.x + 34,
            panel_rect.y + 126);
    }

    desktop_test_window_initialized =
        true;

    return true;
}


/*
 * ------------------------------------------------------------
 * Desktop assets
 * ------------------------------------------------------------
 */

static void gui_desktop_load_images(void)
{
    /*
     * Images are optional presentation assets.
     *
     * Failure must never prevent GUI bootstrap:
     *
     *     wallpaper failure -> gradient fallback
     *     icon failure      -> simply omit validation icon
     */

    desktop_wallpaper = NULL;

    desktop_validation_icon = NULL;

    (void)gui_image_get(
        GUI_DESKTOP_WALLPAPER_PATH,
        &desktop_wallpaper);

    (void)gui_image_get(
        GUI_DESKTOP_VALIDATION_ICON_PATH,
        &desktop_validation_icon);
}


/*
 * ------------------------------------------------------------
 * Desktop bootstrap
 * ------------------------------------------------------------
 */

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
     * EXT2/VFS is already mounted before desktop bootstrap.
     */
    if (!gui_font_system_initialize())
        return false;

    /*
     * Wallpaper and icon files are loaded exactly once and retained
     * through the image cache.
     */
    gui_desktop_load_images();

    if (!gui_desktop_create_test_window())
        return false;

    if (!gui_taskbar_initialize())
        return false;

    desktop_initialized =
        true;

    gui_desktop_render();

    return true;
}


/*
 * ------------------------------------------------------------
 * Scene reconstruction
 * ------------------------------------------------------------
 */

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
     * --------------------------------------------------------
     * GUI_Z_DESKTOP
     * --------------------------------------------------------
     *
     * Always establish an opaque fallback first.
     *
     * This means even a PNG wallpaper containing alpha can never
     * leave stale pixels in the compositor backbuffer.
     */
    gui_surface_fill_vertical_gradient(
        surface,
        desktop_rect,
        theme->desktop_gradient_top,
        theme->desktop_gradient_bottom);

    /*
     * Initial wallpaper milestone intentionally requires native
     * display dimensions.
     *
     * At the current VBE mode:
     *
     *     1360 x 768 wallpaper
     *     1360 x 768 screen
     *
     * Scaling/cropping belongs to a later image-rendering
     * enhancement.
     */
    if (desktop_wallpaper != NULL &&
        gui_image_width(
            desktop_wallpaper) ==
            surface->width &&
        gui_image_height(
            desktop_wallpaper) ==
            surface->height)
    {
        gui_image_draw(
            surface,
            desktop_wallpaper,
            0,
            0);
    }

    /*
     * --------------------------------------------------------
     * GUI_Z_NORMAL
     * --------------------------------------------------------
     */
    gui_window_composite(
        &desktop_test_window);

    /*
     * --------------------------------------------------------
     * GUI_Z_TASKBAR
     * --------------------------------------------------------
     */
    gui_taskbar_composite();

    /*
     * Software cursor remains above the presented scene through
     * the existing framebuffer/cursor coordination.
     */

    gui_compositor_damage_all();

    gui_compositor_present();
}