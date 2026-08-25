#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/gui/painter.h>
#include <kernel/gui/surface.h>


static uint32_t gui_painter_clamp_radius(
    uint32_t width,
    uint32_t height,
    uint32_t radius)
{
    uint32_t maximum =
        width < height
            ? width / 2u
            : height / 2u;

    if (radius > maximum)
        radius = maximum;

    return radius;
}


static bool gui_painter_rounded_contains(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t radius)
{
    if (width == 0u ||
        height == 0u)
    {
        return false;
    }

    radius =
        gui_painter_clamp_radius(
            width,
            height,
            radius);

    if (radius == 0u)
        return true;

    if (x >= radius &&
        x < width - radius)
    {
        return true;
    }

    if (y >= radius &&
        y < height - radius)
    {
        return true;
    }

    int64_t center_x;
    int64_t center_y;

    if (x < radius)
    {
        center_x =
            (int64_t)radius - 1;
    }
    else
    {
        center_x =
            (int64_t)width -
            (int64_t)radius;
    }

    if (y < radius)
    {
        center_y =
            (int64_t)radius - 1;
    }
    else
    {
        center_y =
            (int64_t)height -
            (int64_t)radius;
    }

    int64_t dx =
        (int64_t)x -
        center_x;

    int64_t dy =
        (int64_t)y -
        center_y;

    int64_t radius_squared =
        (int64_t)radius *
        (int64_t)radius;

    return
        dx * dx +
        dy * dy <=
        radius_squared;
}


static uint8_t gui_painter_interpolate_channel(
    uint8_t first,
    uint8_t second,
    uint32_t numerator,
    uint32_t denominator)
{
    if (denominator == 0u)
        return first;

    uint32_t inverse =
        denominator -
        numerator;

    uint32_t value =
        (uint32_t)first *
            inverse +
        (uint32_t)second *
            numerator;

    value +=
        denominator / 2u;

    return
        (uint8_t)
        (value / denominator);
}


static gui_color_t gui_painter_interpolate_color(
    gui_color_t first,
    gui_color_t second,
    uint32_t numerator,
    uint32_t denominator)
{
    return
        GUI_RGBA(
            gui_painter_interpolate_channel(
                gui_color_red(first),
                gui_color_red(second),
                numerator,
                denominator),

            gui_painter_interpolate_channel(
                gui_color_green(first),
                gui_color_green(second),
                numerator,
                denominator),

            gui_painter_interpolate_channel(
                gui_color_blue(first),
                gui_color_blue(second),
                numerator,
                denominator),

            gui_painter_interpolate_channel(
                gui_color_alpha(first),
                gui_color_alpha(second),
                numerator,
                denominator));
}


static gui_color_t gui_painter_color_with_alpha(
    gui_color_t color,
    uint8_t alpha)
{
    return
        GUI_RGBA(
            gui_color_red(color),
            gui_color_green(color),
            gui_color_blue(color),
            alpha);
}


static void gui_painter_fill_rounded_vertical_gradient_internal(
    gui_surface_t *surface,
    gui_rect_t rect,
    uint32_t radius,
    gui_color_t top_color,
    gui_color_t bottom_color,
    bool blend)
{
    if (surface == NULL ||
        surface->pixels == NULL ||
        gui_rect_is_empty(rect))
    {
        return;
    }

    radius =
        gui_painter_clamp_radius(
            rect.width,
            rect.height,
            radius);

    uint32_t denominator =
        rect.height > 1u
            ? rect.height - 1u
            : 1u;

    for (uint32_t local_y = 0u;
         local_y < rect.height;
         ++local_y)
    {
        gui_color_t row_color =
            gui_painter_interpolate_color(
                top_color,
                bottom_color,
                local_y,
                denominator);

        for (uint32_t local_x = 0u;
             local_x < rect.width;
             ++local_x)
        {
            if (!gui_painter_rounded_contains(
                    local_x,
                    local_y,
                    rect.width,
                    rect.height,
                    radius))
            {
                continue;
            }

            int64_t destination_x =
                (int64_t)rect.x +
                (int64_t)local_x;

            int64_t destination_y =
                (int64_t)rect.y +
                (int64_t)local_y;

            if (destination_x < INT32_MIN ||
                destination_x > INT32_MAX ||
                destination_y < INT32_MIN ||
                destination_y > INT32_MAX)
            {
                continue;
            }

            if (blend)
            {
                gui_surface_blend_pixel(
                    surface,
                    (int32_t)destination_x,
                    (int32_t)destination_y,
                    row_color);
            }
            else
            {
                gui_surface_put_pixel(
                    surface,
                    (int32_t)destination_x,
                    (int32_t)destination_y,
                    row_color);
            }
        }
    }
}


void gui_painter_fill_rounded_vertical_gradient(
    gui_surface_t *surface,
    gui_rect_t rect,
    uint32_t radius,
    gui_color_t top_color,
    gui_color_t bottom_color)
{
    gui_painter_fill_rounded_vertical_gradient_internal(
        surface,
        rect,
        radius,
        top_color,
        bottom_color,
        false);
}


void gui_painter_fill_rounded_vertical_gradient_blend(
    gui_surface_t *surface,
    gui_rect_t rect,
    uint32_t radius,
    gui_color_t top_color,
    gui_color_t bottom_color)
{
    gui_painter_fill_rounded_vertical_gradient_internal(
        surface,
        rect,
        radius,
        top_color,
        bottom_color,
        true);
}


void gui_painter_stroke_rounded_rect(
    gui_surface_t *surface,
    gui_rect_t rect,
    uint32_t radius,
    uint32_t thickness,
    gui_color_t color)
{
    if (surface == NULL ||
        surface->pixels == NULL ||
        gui_rect_is_empty(rect) ||
        thickness == 0u)
    {
        return;
    }

    radius =
        gui_painter_clamp_radius(
            rect.width,
            rect.height,
            radius);

    bool has_inner =
        rect.width > thickness * 2u &&
        rect.height > thickness * 2u;

    uint32_t inner_width = 0u;
    uint32_t inner_height = 0u;
    uint32_t inner_radius = 0u;

    if (has_inner)
    {
        inner_width =
            rect.width -
            thickness * 2u;

        inner_height =
            rect.height -
            thickness * 2u;

        inner_radius =
            radius > thickness
                ? radius - thickness
                : 0u;
    }

    for (uint32_t local_y = 0u;
         local_y < rect.height;
         ++local_y)
    {
        for (uint32_t local_x = 0u;
             local_x < rect.width;
             ++local_x)
        {
            if (!gui_painter_rounded_contains(
                    local_x,
                    local_y,
                    rect.width,
                    rect.height,
                    radius))
            {
                continue;
            }

            bool inside_inner =
                false;

            if (has_inner &&
                local_x >= thickness &&
                local_y >= thickness &&
                local_x <
                    rect.width - thickness &&
                local_y <
                    rect.height - thickness)
            {
                inside_inner =
                    gui_painter_rounded_contains(
                        local_x - thickness,
                        local_y - thickness,
                        inner_width,
                        inner_height,
                        inner_radius);
            }

            if (inside_inner)
                continue;

            int64_t destination_x =
                (int64_t)rect.x +
                (int64_t)local_x;

            int64_t destination_y =
                (int64_t)rect.y +
                (int64_t)local_y;

            if (destination_x < INT32_MIN ||
                destination_x > INT32_MAX ||
                destination_y < INT32_MIN ||
                destination_y > INT32_MAX)
            {
                continue;
            }

            gui_surface_blend_pixel(
                surface,
                (int32_t)destination_x,
                (int32_t)destination_y,
                color);
        }
    }
}


void gui_painter_draw_rounded_shadow(
    gui_surface_t *surface,
    gui_rect_t rect,
    uint32_t radius,
    int32_t offset_x,
    int32_t offset_y,
    uint32_t spread,
    uint32_t blur_radius,
    gui_color_t color)
{
    if (surface == NULL ||
        surface->pixels == NULL ||
        gui_rect_is_empty(rect))
    {
        return;
    }

    uint8_t base_alpha =
        gui_color_alpha(color);

    if (base_alpha == 0u)
        return;

    /*
     * Start with the solid/spread portion.
     */
    gui_rect_t shadow_rect;

    shadow_rect.x =
        rect.x +
        offset_x -
        (int32_t)spread;

    shadow_rect.y =
        rect.y +
        offset_y -
        (int32_t)spread;

    shadow_rect.width =
        rect.width +
        spread * 2u;

    shadow_rect.height =
        rect.height +
        spread * 2u;

    uint32_t shadow_radius =
        radius + spread;

    /*
     * Paint blur rings from outside toward the solid shadow.
     *
     * Each ring is independent rather than repeatedly filling the
     * entire shadow. This prevents the outer falloff from becoming
     * excessively opaque through repeated source-over blending.
     */
    if (blur_radius != 0u)
    {
        for (uint32_t distance = blur_radius;
             distance > 0u;
             --distance)
        {
            gui_rect_t blur_rect;

            blur_rect.x =
                shadow_rect.x -
                (int32_t)distance;

            blur_rect.y =
                shadow_rect.y -
                (int32_t)distance;

            blur_rect.width =
                shadow_rect.width +
                distance * 2u;

            blur_rect.height =
                shadow_rect.height +
                distance * 2u;

            uint32_t numerator =
                blur_radius -
                distance +
                1u;

            uint32_t layer_alpha =
                ((uint32_t)base_alpha *
                 numerator) /
                (blur_radius + 1u);

            if (layer_alpha == 0u)
                layer_alpha = 1u;

            gui_color_t layer_color =
                gui_painter_color_with_alpha(
                    color,
                    (uint8_t)layer_alpha);

            gui_painter_stroke_rounded_rect(
                surface,
                blur_rect,
                shadow_radius + distance,
                1u,
                layer_color);
        }
    }

    /*
     * Solid center of the shadow.
     *
     * The actual panel is drawn afterward, so the portion directly
     * underneath it will naturally be hidden.
     */
    gui_surface_fill_rounded_rect_blend(
        surface,
        shadow_rect,
        shadow_radius,
        color);
}