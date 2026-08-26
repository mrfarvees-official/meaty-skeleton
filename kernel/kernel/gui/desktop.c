#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/desktop.h>
#include <kernel/gui/font.h>
#include <kernel/gui/image.h>
#include <kernel/gui/painter.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/taskbar.h>
#include <kernel/gui/theme.h>
#include <kernel/gui/window.h>
#include <kernel/gui/topbar.h>


/*
 * ------------------------------------------------------------
 * Desktop assets
 * ------------------------------------------------------------
 */

#define GUI_DESKTOP_DEFAULT_WALLPAPER_PATH \
    "/wallpapers/default.png"

#define GUI_DESKTOP_VALIDATION_ICON_PATH \
    "/icons/apps/terminal.png"

#define GUI_DESKTOP_WALLPAPER_PATH_CAPACITY \
    256u


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
 * ------------------------------------------------------------
 * Wallpaper state
 * ------------------------------------------------------------
 */

static const gui_image_t *
    desktop_wallpaper;

static char
    desktop_wallpaper_path[
        GUI_DESKTOP_WALLPAPER_PATH_CAPACITY];

static gui_wallpaper_mode_t
    desktop_wallpaper_mode =
        GUI_WALLPAPER_FILL;


/*
 * Temporary PNG-alpha validation asset.
 */
static const gui_image_t *
    desktop_validation_icon;


/*
 * ------------------------------------------------------------
 * Wallpaper geometry helpers
 * ------------------------------------------------------------
 */

static uint32_t gui_desktop_nonzero_dimension(
    uint64_t value)
{
    if (value == 0u)
        return 1u;

    if (value > UINT32_MAX)
        return UINT32_MAX;

    return (uint32_t)value;
}


static gui_rect_t gui_desktop_wallpaper_rect(
    const gui_surface_t *screen,
    const gui_image_t *wallpaper,
    gui_wallpaper_mode_t mode)
{
    gui_rect_t rect;

    rect.x = 0;
    rect.y = 0;

    rect.width =
        screen != NULL
            ? screen->width
            : 0u;

    rect.height =
        screen != NULL
            ? screen->height
            : 0u;

    if (screen == NULL ||
        wallpaper == NULL ||
        screen->width == 0u ||
        screen->height == 0u)
    {
        return rect;
    }

    uint32_t image_width =
        gui_image_width(
            wallpaper);

    uint32_t image_height =
        gui_image_height(
            wallpaper);

    if (image_width == 0u ||
        image_height == 0u)
    {
        return rect;
    }

    switch (mode)
    {
    case GUI_WALLPAPER_STRETCH:
        /*
         * Already initialized to complete screen dimensions.
         */
        break;

    case GUI_WALLPAPER_CENTER:
        rect.width =
            image_width;

        rect.height =
            image_height;

        rect.x =
            ((int32_t)screen->width -
             (int32_t)rect.width) /
            2;

        rect.y =
            ((int32_t)screen->height -
             (int32_t)rect.height) /
            2;

        break;

    case GUI_WALLPAPER_FIT:
    {
        /*
         * Compare aspect ratios without floating point:
         *
         *     image_width / image_height
         *     screen_width / screen_height
         */
        uint64_t image_cross =
            (uint64_t)image_width *
            screen->height;

        uint64_t screen_cross =
            (uint64_t)screen->width *
            image_height;

        if (image_cross > screen_cross)
        {
            /*
             * Image is relatively wider.
             * Width determines the scale.
             */
            rect.width =
                screen->width;

            rect.height =
                gui_desktop_nonzero_dimension(
                    ((uint64_t)image_height *
                     screen->width) /
                    image_width);
        }
        else
        {
            /*
             * Image is relatively taller.
             * Height determines the scale.
             */
            rect.height =
                screen->height;

            rect.width =
                gui_desktop_nonzero_dimension(
                    ((uint64_t)image_width *
                     screen->height) /
                    image_height);
        }

        rect.x =
            ((int32_t)screen->width -
             (int32_t)rect.width) /
            2;

        rect.y =
            ((int32_t)screen->height -
             (int32_t)rect.height) /
            2;

        break;
    }

    case GUI_WALLPAPER_FILL:
    default:
    {
        uint64_t image_cross =
            (uint64_t)image_width *
            screen->height;

        uint64_t screen_cross =
            (uint64_t)screen->width *
            image_height;

        if (image_cross > screen_cross)
        {
            /*
             * Image is relatively wider.
             *
             * Height must match the screen; width extends beyond
             * it and will be center-cropped.
             */
            rect.height =
                screen->height;

            rect.width =
                gui_desktop_nonzero_dimension(
                    ((uint64_t)image_width *
                     screen->height +
                     image_height - 1u) /
                    image_height);
        }
        else
        {
            /*
             * Image is relatively taller.
             *
             * Width must match the screen; height extends beyond
             * it and will be center-cropped.
             */
            rect.width =
                screen->width;

            rect.height =
                gui_desktop_nonzero_dimension(
                    ((uint64_t)image_height *
                     screen->width +
                     image_width - 1u) /
                    image_width);
        }

        rect.x =
            ((int32_t)screen->width -
             (int32_t)rect.width) /
            2;

        rect.y =
            ((int32_t)screen->height -
             (int32_t)rect.height) /
            2;

        break;
    }
    }

    return rect;
}


/*
 * ------------------------------------------------------------
 * Wallpaper public API
 * ------------------------------------------------------------
 */

bool gui_desktop_set_wallpaper(
    const char *path,
    gui_wallpaper_mode_t mode)
{
    if (path == NULL ||
        path[0] == '\0')
    {
        return false;
    }

    if (mode != GUI_WALLPAPER_STRETCH &&
        mode != GUI_WALLPAPER_FIT &&
        mode != GUI_WALLPAPER_FILL &&
        mode != GUI_WALLPAPER_CENTER)
    {
        return false;
    }

    size_t path_length =
        strlen(path);

    if (path_length >=
        GUI_DESKTOP_WALLPAPER_PATH_CAPACITY)
    {
        return false;
    }

    const gui_image_t *image =
        NULL;

    /*
     * Do not replace the current wallpaper unless the new image
     * loads successfully.
     */
    if (!gui_image_get(
            path,
            &image))
    {
        return false;
    }

    if (image == NULL ||
        gui_image_width(image) == 0u ||
        gui_image_height(image) == 0u)
    {
        return false;
    }

    memcpy(
        desktop_wallpaper_path,
        path,
        path_length + 1u);

    desktop_wallpaper =
        image;

    desktop_wallpaper_mode =
        mode;

    /*
     * Before bootstrap this only updates configuration.
     *
     * After bootstrap the change becomes visible immediately.
     */
    if (desktop_initialized)
        gui_desktop_render();

    return true;
}


const char *gui_desktop_wallpaper_path(void)
{
    if (desktop_wallpaper == NULL ||
        desktop_wallpaper_path[0] == '\0')
    {
        return NULL;
    }

    return
        desktop_wallpaper_path;
}


gui_wallpaper_mode_t gui_desktop_wallpaper_mode(void)
{
    return
        desktop_wallpaper_mode;
}


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
 * Optional validation assets
 * ------------------------------------------------------------
 */

static void gui_desktop_load_validation_icon(void)
{
    desktop_validation_icon =
        NULL;

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

    if (!gui_font_system_initialize())
        return false;

    /*
     * Default wallpaper remains optional.
     */
    if (desktop_wallpaper == NULL)
    {
        (void)gui_desktop_set_wallpaper(
            GUI_DESKTOP_DEFAULT_WALLPAPER_PATH,
            GUI_WALLPAPER_FILL);
    }

    gui_desktop_load_validation_icon();

    if (!gui_desktop_create_test_window())
        return false;

    /*
     * Desktop shell chrome.
     */
    if (!gui_topbar_initialize())
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
     */
    gui_surface_fill_vertical_gradient(
        surface,
        desktop_rect,
        theme->desktop_gradient_top,
        theme->desktop_gradient_bottom);

    if (desktop_wallpaper != NULL)
    {
        gui_rect_t wallpaper_rect =
            gui_desktop_wallpaper_rect(
                surface,
                desktop_wallpaper,
                desktop_wallpaper_mode);

        gui_image_draw_scaled(
            surface,
            desktop_wallpaper,
            wallpaper_rect);
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
     * Desktop shell chrome
     * --------------------------------------------------------
     *
     * Ordering remains explicit until the window manager owns
     * z-sorted windows.
     */
    gui_topbar_composite();

    gui_taskbar_composite();

    gui_compositor_damage_all();

    gui_compositor_present();
}


