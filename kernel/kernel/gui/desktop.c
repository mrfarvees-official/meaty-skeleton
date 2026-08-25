#include <stdbool.h>
#include <stddef.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/desktop.h>
#include <kernel/gui/font.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/theme.h>
#include <kernel/gui/window.h>


/*
 * ------------------------------------------------------------
 * Temporary theme validation window
 * ------------------------------------------------------------
 *
 * The surface includes room around the visible panel for its
 * composited shadow.
 */

#define GUI_TEST_WINDOW_WIDTH \
    500u

#define GUI_TEST_WINDOW_HEIGHT \
    320u

#define GUI_TEST_WINDOW_MARGIN \
    20u

#define GUI_TEST_SHADOW_OFFSET_Y \
    8

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

    if (surface == NULL)
    {
        gui_window_destroy(
            &desktop_test_window);

        return false;
    }

    /*
     * Transparent window backing.
     *
     * This is what allows rounded corners and the shadow to reveal
     * the desktop through the unused parts of the surface.
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
     * Very simple first soft-look shadow.
     *
     * Proper blurred shadows can come later. For this milestone
     * the translucent expanded rounded rectangle validates alpha
     * composition without introducing a blur engine.
     */
    gui_rect_t shadow_rect =
        panel_rect;

    shadow_rect.x -= 4;
    shadow_rect.y +=
        GUI_TEST_SHADOW_OFFSET_Y;

    shadow_rect.width += 8u;
    shadow_rect.height += 4u;

    gui_surface_fill_rounded_rect_blend(
        surface,
        shadow_rect,
        theme->window_corner_radius + 4u,
        theme->window_shadow);

    /*
     * Start the panel transparent and build its gradient only
     * inside the rounded mask.
     *
     * Since the current gradient primitive is rectangular, build
     * a temporary gradient by drawing one rounded row at a time.
     */
    uint32_t gradient_denominator =
        panel_rect.height > 1u
            ? panel_rect.height - 1u
            : 1u;

    for (uint32_t row = 0u;
         row < panel_rect.height;
         ++row)
    {
        uint32_t inverse =
            gradient_denominator - row;

        uint32_t top_alpha =
            gui_color_alpha(
                theme->window_gradient_top);

        uint32_t bottom_alpha =
            gui_color_alpha(
                theme->window_gradient_bottom);

        uint32_t top_red =
            gui_color_red(
                theme->window_gradient_top);

        uint32_t bottom_red =
            gui_color_red(
                theme->window_gradient_bottom);

        uint32_t top_green =
            gui_color_green(
                theme->window_gradient_top);

        uint32_t bottom_green =
            gui_color_green(
                theme->window_gradient_bottom);

        uint32_t top_blue =
            gui_color_blue(
                theme->window_gradient_top);

        uint32_t bottom_blue =
            gui_color_blue(
                theme->window_gradient_bottom);

        uint32_t red =
            (top_red * inverse +
             bottom_red * row +
             gradient_denominator / 2u) /
            gradient_denominator;

        uint32_t green =
            (top_green * inverse +
             bottom_green * row +
             gradient_denominator / 2u) /
            gradient_denominator;

        uint32_t blue =
            (top_blue * inverse +
             bottom_blue * row +
             gradient_denominator / 2u) /
            gradient_denominator;

        uint32_t alpha =
            (top_alpha * inverse +
             bottom_alpha * row +
             gradient_denominator / 2u) /
            gradient_denominator;

        gui_rect_t row_rect;

        row_rect.x =
            panel_rect.x;

        row_rect.y =
            panel_rect.y +
            (int32_t)row;

        row_rect.width =
            panel_rect.width;

        row_rect.height =
            1u;

        /*
         * Only draw row pixels lying inside the complete rounded
         * panel. The general rounded primitive supplies that mask.
         *
         * Drawing the complete panel repeatedly would be wasteful,
         * so the panel gradient is first rectangular and the
         * transparent surface around it remains untouched by
         * explicitly clearing the corner pixels below.
         */
        gui_surface_fill_rect(
            surface,
            row_rect,
            GUI_RGBA(
                red,
                green,
                blue,
                alpha));
    }

    /*
     * Cut the four square corners back to transparency.
     *
     * Then repaint the rounded panel using a translucent overlay.
     *
     * For this validation milestone this keeps the primitive set
     * small. A clipped gradient/mask API should come later with
     * widgets rather than prematurely building a full painter.
     */

    uint32_t radius =
        theme->window_corner_radius;

    for (uint32_t local_y = 0u;
         local_y < panel_rect.height;
         ++local_y)
    {
        for (uint32_t local_x = 0u;
             local_x < panel_rect.width;
             ++local_x)
        {
            bool middle_x =
                local_x >= radius &&
                local_x <
                    panel_rect.width - radius;

            bool middle_y =
                local_y >= radius &&
                local_y <
                    panel_rect.height - radius;

            if (middle_x || middle_y)
                continue;

            int64_t center_x =
                local_x < radius
                    ? (int64_t)radius - 1
                    : (int64_t)panel_rect.width -
                        (int64_t)radius;

            int64_t center_y =
                local_y < radius
                    ? (int64_t)radius - 1
                    : (int64_t)panel_rect.height -
                        (int64_t)radius;

            int64_t dx =
                (int64_t)local_x -
                center_x;

            int64_t dy =
                (int64_t)local_y -
                center_y;

            if (dx * dx + dy * dy >
                (int64_t)radius *
                (int64_t)radius)
            {
                gui_surface_put_pixel(
                    surface,
                    panel_rect.x +
                        (int32_t)local_x,
                    panel_rect.y +
                        (int32_t)local_y,
                    GUI_TRANSPARENT);
            }
        }
    }

    /*
     * Subtle translucent border.
     *
     * The first milestone does not need a dedicated stroked-rounded-
     * rectangle primitive yet, so two nested rounded rectangles give
     * us a simple border while retaining the translucent interior.
     */
    gui_surface_fill_rounded_rect_blend(
        surface,
        panel_rect,
        radius,
        theme->window_border);

    gui_rect_t inner_rect;

    inner_rect.x =
        panel_rect.x + 1;

    inner_rect.y =
        panel_rect.y + 1;

    inner_rect.width =
        panel_rect.width - 2u;

    inner_rect.height =
        panel_rect.height - 2u;

    gui_surface_fill_rounded_rect_blend(
        surface,
        inner_rect,
        radius > 1u
            ? radius - 1u
            : 0u,
        GUI_RGBA(255u, 255u, 255u, 12u));

    /*
     * Keep the existing font validation, now rendered into an
     * alpha-capable themed surface.
     */
    if (!gui_font_draw_text(
            surface,
            font,
            panel_rect.x + 32,
            panel_rect.y + 30,
            GUI_TEST_TEXT_PIXEL_HEIGHT,
            "Meaty OS",
            theme->text_primary))
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
     * The desktop is deliberately opaque.
     *
     * Therefore the compositor's final backbuffer always has a
     * deterministic opaque base behind translucent windows.
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