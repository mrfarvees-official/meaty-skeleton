#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/gui/surface.h>
#include <kernel/heap.h>


static bool gui_surface_dimensions_valid(
    uint32_t width,
    uint32_t height)
{
    if (width == 0u ||
        height == 0u)
    {
        return false;
    }

    if (width >
        UINT32_MAX /
            sizeof(gui_color_t))
    {
        return false;
    }

    size_t pitch =
        (size_t)width *
        sizeof(gui_color_t);

    if ((size_t)height >
        SIZE_MAX / pitch)
    {
        return false;
    }

    return true;
}


static bool gui_surface_get_pixel_pointer(
    gui_surface_t *surface,
    int32_t x,
    int32_t y,
    gui_color_t **result)
{
    if (result == NULL)
        return false;

    *result = NULL;

    if (surface == NULL ||
        surface->pixels == NULL)
    {
        return false;
    }

    if (x < 0 ||
        y < 0)
    {
        return false;
    }

    if ((uint32_t)x >= surface->width ||
        (uint32_t)y >= surface->height)
    {
        return false;
    }

    uint8_t *row =
        (uint8_t *)surface->pixels +
        (size_t)(uint32_t)y *
            surface->pitch;

    *result =
        (gui_color_t *)row +
        (uint32_t)x;

    return true;
}


static uint8_t gui_color_interpolate_channel(
    uint8_t first,
    uint8_t second,
    uint32_t numerator,
    uint32_t denominator)
{
    if (denominator == 0u)
        return first;

    uint32_t inverse =
        denominator - numerator;

    uint32_t value =
        (uint32_t)first * inverse +
        (uint32_t)second * numerator;

    value += denominator / 2u;

    return
        (uint8_t)
        (value / denominator);
}


static gui_color_t gui_color_interpolate(
    gui_color_t first,
    gui_color_t second,
    uint32_t numerator,
    uint32_t denominator)
{
    return
        GUI_RGBA(
            gui_color_interpolate_channel(
                gui_color_red(first),
                gui_color_red(second),
                numerator,
                denominator),
            gui_color_interpolate_channel(
                gui_color_green(first),
                gui_color_green(second),
                numerator,
                denominator),
            gui_color_interpolate_channel(
                gui_color_blue(first),
                gui_color_blue(second),
                numerator,
                denominator),
            gui_color_interpolate_channel(
                gui_color_alpha(first),
                gui_color_alpha(second),
                numerator,
                denominator));
}


static uint32_t gui_rounded_rect_clamp_radius(
    gui_rect_t rect,
    uint32_t radius)
{
    uint32_t maximum =
        rect.width < rect.height
            ? rect.width / 2u
            : rect.height / 2u;

    if (radius > maximum)
        radius = maximum;

    return radius;
}


static bool gui_rounded_rect_contains_local_point(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t radius)
{
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
        center_x = (int64_t)radius - 1;
    else
        center_x =
            (int64_t)width -
            (int64_t)radius;

    if (y < radius)
        center_y = (int64_t)radius - 1;
    else
        center_y =
            (int64_t)height -
            (int64_t)radius;

    int64_t dx =
        (int64_t)x - center_x;

    int64_t dy =
        (int64_t)y - center_y;

    int64_t radius_squared =
        (int64_t)radius *
        (int64_t)radius;

    return
        dx * dx +
        dy * dy <=
        radius_squared;
}


uint8_t gui_color_alpha(
    gui_color_t color)
{
    return
        (uint8_t)
        ((color >> 24) & 0xFFu);
}


uint8_t gui_color_red(
    gui_color_t color)
{
    return
        (uint8_t)
        ((color >> 16) & 0xFFu);
}


uint8_t gui_color_green(
    gui_color_t color)
{
    return
        (uint8_t)
        ((color >> 8) & 0xFFu);
}


uint8_t gui_color_blue(
    gui_color_t color)
{
    return
        (uint8_t)
        (color & 0xFFu);
}


gui_color_t gui_color_blend(
    gui_color_t destination,
    gui_color_t source)
{
    uint32_t source_alpha =
        gui_color_alpha(source);

    if (source_alpha == 0u)
        return destination;

    if (source_alpha == 255u)
        return source;

    uint32_t destination_alpha =
        gui_color_alpha(destination);

    uint32_t inverse_source_alpha =
        255u - source_alpha;

    /*
     * out_alpha_numerator is alpha scaled by 255:
     *
     *     As * 255 + Ad * (255 - As)
     */
    uint32_t out_alpha_numerator =
        source_alpha * 255u +
        destination_alpha *
            inverse_source_alpha;

    if (out_alpha_numerator == 0u)
        return GUI_TRANSPARENT;

    uint32_t out_alpha =
        (out_alpha_numerator + 127u) /
        255u;

    uint32_t red_numerator =
        (uint32_t)gui_color_red(source) *
            source_alpha *
            255u +
        (uint32_t)gui_color_red(destination) *
            destination_alpha *
            inverse_source_alpha;

    uint32_t green_numerator =
        (uint32_t)gui_color_green(source) *
            source_alpha *
            255u +
        (uint32_t)gui_color_green(destination) *
            destination_alpha *
            inverse_source_alpha;

    uint32_t blue_numerator =
        (uint32_t)gui_color_blue(source) *
            source_alpha *
            255u +
        (uint32_t)gui_color_blue(destination) *
            destination_alpha *
            inverse_source_alpha;

    uint32_t red =
        (red_numerator +
         out_alpha_numerator / 2u) /
        out_alpha_numerator;

    uint32_t green =
        (green_numerator +
         out_alpha_numerator / 2u) /
        out_alpha_numerator;

    uint32_t blue =
        (blue_numerator +
         out_alpha_numerator / 2u) /
        out_alpha_numerator;

    return
        GUI_RGBA(
            red,
            green,
            blue,
            out_alpha);
}


bool gui_surface_create(
    gui_surface_t *surface,
    uint32_t width,
    uint32_t height)
{
    if (surface == NULL)
        return false;

    memset(
        surface,
        0,
        sizeof(*surface));

    if (!gui_surface_dimensions_valid(
            width,
            height))
    {
        return false;
    }

    size_t pitch =
        (size_t)width *
        sizeof(gui_color_t);

    size_t size =
        pitch *
        (size_t)height;

    gui_color_t *pixels =
        kmalloc(size);

    if (pixels == NULL)
        return false;

    memset(
        pixels,
        0,
        size);

    surface->width =
        width;

    surface->height =
        height;

    surface->pitch =
        (uint32_t)pitch;

    surface->pixels =
        pixels;

    surface->owns_pixels =
        true;

    return true;
}


void gui_surface_destroy(
    gui_surface_t *surface)
{
    if (surface == NULL)
        return;

    if (surface->owns_pixels &&
        surface->pixels != NULL)
    {
        kfree(
            surface->pixels);
    }

    memset(
        surface,
        0,
        sizeof(*surface));
}


void gui_surface_put_pixel(
    gui_surface_t *surface,
    int32_t x,
    int32_t y,
    gui_color_t color)
{
    gui_color_t *pixel = NULL;

    if (!gui_surface_get_pixel_pointer(
            surface,
            x,
            y,
            &pixel))
    {
        return;
    }

    *pixel = color;
}


void gui_surface_blend_pixel(
    gui_surface_t *surface,
    int32_t x,
    int32_t y,
    gui_color_t color)
{
    gui_color_t *pixel = NULL;

    if (!gui_surface_get_pixel_pointer(
            surface,
            x,
            y,
            &pixel))
    {
        return;
    }

    *pixel =
        gui_color_blend(
            *pixel,
            color);
}


void gui_surface_clear(
    gui_surface_t *surface,
    gui_color_t color)
{
    if (surface == NULL ||
        surface->pixels == NULL)
    {
        return;
    }

    gui_rect_t rect;

    rect.x = 0;
    rect.y = 0;
    rect.width = surface->width;
    rect.height = surface->height;

    gui_surface_fill_rect(
        surface,
        rect,
        color);
}


bool gui_rect_is_empty(
    gui_rect_t rect)
{
    return
        rect.width == 0u ||
        rect.height == 0u;
}


bool gui_rect_intersect(
    gui_rect_t first,
    gui_rect_t second,
    gui_rect_t *result)
{
    if (result == NULL)
        return false;

    result->x = 0;
    result->y = 0;
    result->width = 0u;
    result->height = 0u;

    if (gui_rect_is_empty(first) ||
        gui_rect_is_empty(second))
    {
        return false;
    }

    int64_t first_left =
        first.x;

    int64_t first_top =
        first.y;

    int64_t first_right =
        first_left +
        first.width;

    int64_t first_bottom =
        first_top +
        first.height;

    int64_t second_left =
        second.x;

    int64_t second_top =
        second.y;

    int64_t second_right =
        second_left +
        second.width;

    int64_t second_bottom =
        second_top +
        second.height;

    int64_t left =
        first_left >
                second_left
            ? first_left
            : second_left;

    int64_t top =
        first_top >
                second_top
            ? first_top
            : second_top;

    int64_t right =
        first_right <
                second_right
            ? first_right
            : second_right;

    int64_t bottom =
        first_bottom <
                second_bottom
            ? first_bottom
            : second_bottom;

    if (right <= left ||
        bottom <= top)
    {
        return false;
    }

    result->x =
        (int32_t)left;

    result->y =
        (int32_t)top;

    result->width =
        (uint32_t)
        (right - left);

    result->height =
        (uint32_t)
        (bottom - top);

    return true;
}


bool gui_rect_union(
    gui_rect_t first,
    gui_rect_t second,
    gui_rect_t *result)
{
    if (result == NULL)
        return false;

    if (gui_rect_is_empty(first))
    {
        *result = second;

        return
            !gui_rect_is_empty(
                second);
    }

    if (gui_rect_is_empty(second))
    {
        *result = first;

        return true;
    }

    int64_t first_right =
        (int64_t)first.x +
        first.width;

    int64_t first_bottom =
        (int64_t)first.y +
        first.height;

    int64_t second_right =
        (int64_t)second.x +
        second.width;

    int64_t second_bottom =
        (int64_t)second.y +
        second.height;

    int64_t left =
        first.x <
                second.x
            ? first.x
            : second.x;

    int64_t top =
        first.y <
                second.y
            ? first.y
            : second.y;

    int64_t right =
        first_right >
                second_right
            ? first_right
            : second_right;

    int64_t bottom =
        first_bottom >
                second_bottom
            ? first_bottom
            : second_bottom;

    if (right <= left ||
        bottom <= top)
    {
        return false;
    }

    result->x =
        (int32_t)left;

    result->y =
        (int32_t)top;

    result->width =
        (uint32_t)
        (right - left);

    result->height =
        (uint32_t)
        (bottom - top);

    return true;
}


void gui_surface_fill_rect(
    gui_surface_t *surface,
    gui_rect_t rect,
    gui_color_t color)
{
    if (surface == NULL ||
        surface->pixels == NULL)
    {
        return;
    }

    gui_rect_t bounds;

    bounds.x = 0;
    bounds.y = 0;
    bounds.width =
        surface->width;
    bounds.height =
        surface->height;

    gui_rect_t clipped;

    if (!gui_rect_intersect(
            rect,
            bounds,
            &clipped))
    {
        return;
    }

    for (uint32_t y = 0u;
         y < clipped.height;
         ++y)
    {
        uint32_t destination_y =
            (uint32_t)clipped.y +
            y;

        uint8_t *row =
            (uint8_t *)surface->pixels +
            (size_t)destination_y *
                surface->pitch;

        gui_color_t *destination =
            (gui_color_t *)row +
            (uint32_t)clipped.x;

        for (uint32_t x = 0u;
             x < clipped.width;
             ++x)
        {
            destination[x] =
                color;
        }
    }
}


void gui_surface_fill_rect_blend(
    gui_surface_t *surface,
    gui_rect_t rect,
    gui_color_t color)
{
    if (surface == NULL ||
        surface->pixels == NULL)
    {
        return;
    }

    gui_rect_t bounds;

    bounds.x = 0;
    bounds.y = 0;
    bounds.width = surface->width;
    bounds.height = surface->height;

    gui_rect_t clipped;

    if (!gui_rect_intersect(
            rect,
            bounds,
            &clipped))
    {
        return;
    }

    for (uint32_t y = 0u;
         y < clipped.height;
         ++y)
    {
        for (uint32_t x = 0u;
             x < clipped.width;
             ++x)
        {
            gui_surface_blend_pixel(
                surface,
                clipped.x + (int32_t)x,
                clipped.y + (int32_t)y,
                color);
        }
    }
}


void gui_surface_fill_vertical_gradient(
    gui_surface_t *surface,
    gui_rect_t rect,
    gui_color_t top_color,
    gui_color_t bottom_color)
{
    if (surface == NULL ||
        surface->pixels == NULL ||
        rect.height == 0u)
    {
        return;
    }

    gui_rect_t bounds;

    bounds.x = 0;
    bounds.y = 0;
    bounds.width = surface->width;
    bounds.height = surface->height;

    gui_rect_t clipped;

    if (!gui_rect_intersect(
            rect,
            bounds,
            &clipped))
    {
        return;
    }

    uint32_t denominator =
        rect.height > 1u
            ? rect.height - 1u
            : 1u;

    for (uint32_t y = 0u;
         y < clipped.height;
         ++y)
    {
        uint32_t absolute_y =
            (uint32_t)clipped.y +
            y;

        uint32_t gradient_y =
            (uint32_t)
            ((int64_t)absolute_y -
             (int64_t)rect.y);

        if (gradient_y > denominator)
            gradient_y = denominator;

        gui_color_t color =
            gui_color_interpolate(
                top_color,
                bottom_color,
                gradient_y,
                denominator);

        gui_rect_t row;

        row.x = clipped.x;
        row.y = (int32_t)absolute_y;
        row.width = clipped.width;
        row.height = 1u;

        gui_surface_fill_rect(
            surface,
            row,
            color);
    }
}


void gui_surface_fill_vertical_gradient_blend(
    gui_surface_t *surface,
    gui_rect_t rect,
    gui_color_t top_color,
    gui_color_t bottom_color)
{
    if (surface == NULL ||
        surface->pixels == NULL ||
        rect.height == 0u)
    {
        return;
    }

    gui_rect_t bounds;

    bounds.x = 0;
    bounds.y = 0;
    bounds.width = surface->width;
    bounds.height = surface->height;

    gui_rect_t clipped;

    if (!gui_rect_intersect(
            rect,
            bounds,
            &clipped))
    {
        return;
    }

    uint32_t denominator =
        rect.height > 1u
            ? rect.height - 1u
            : 1u;

    for (uint32_t y = 0u;
         y < clipped.height;
         ++y)
    {
        uint32_t absolute_y =
            (uint32_t)clipped.y +
            y;

        uint32_t gradient_y =
            (uint32_t)
            ((int64_t)absolute_y -
             (int64_t)rect.y);

        if (gradient_y > denominator)
            gradient_y = denominator;

        gui_color_t color =
            gui_color_interpolate(
                top_color,
                bottom_color,
                gradient_y,
                denominator);

        gui_rect_t row;

        row.x = clipped.x;
        row.y = (int32_t)absolute_y;
        row.width = clipped.width;
        row.height = 1u;

        gui_surface_fill_rect_blend(
            surface,
            row,
            color);
    }
}


void gui_surface_fill_rounded_rect(
    gui_surface_t *surface,
    gui_rect_t rect,
    uint32_t radius,
    gui_color_t color)
{
    if (surface == NULL ||
        surface->pixels == NULL ||
        gui_rect_is_empty(rect))
    {
        return;
    }

    radius =
        gui_rounded_rect_clamp_radius(
            rect,
            radius);

    for (uint32_t y = 0u;
         y < rect.height;
         ++y)
    {
        for (uint32_t x = 0u;
             x < rect.width;
             ++x)
        {
            if (!gui_rounded_rect_contains_local_point(
                    x,
                    y,
                    rect.width,
                    rect.height,
                    radius))
            {
                continue;
            }

            gui_surface_put_pixel(
                surface,
                rect.x + (int32_t)x,
                rect.y + (int32_t)y,
                color);
        }
    }
}


void gui_surface_fill_rounded_rect_blend(
    gui_surface_t *surface,
    gui_rect_t rect,
    uint32_t radius,
    gui_color_t color)
{
    if (surface == NULL ||
        surface->pixels == NULL ||
        gui_rect_is_empty(rect))
    {
        return;
    }

    radius =
        gui_rounded_rect_clamp_radius(
            rect,
            radius);

    for (uint32_t y = 0u;
         y < rect.height;
         ++y)
    {
        for (uint32_t x = 0u;
             x < rect.width;
             ++x)
        {
            if (!gui_rounded_rect_contains_local_point(
                    x,
                    y,
                    rect.width,
                    rect.height,
                    radius))
            {
                continue;
            }

            gui_surface_blend_pixel(
                surface,
                rect.x + (int32_t)x,
                rect.y + (int32_t)y,
                color);
        }
    }
}