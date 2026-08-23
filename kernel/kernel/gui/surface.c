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
    if (surface == NULL ||
        surface->pixels == NULL)
    {
        return;
    }

    if (x < 0 ||
        y < 0)
    {
        return;
    }

    if ((uint32_t)x >=
            surface->width ||
        (uint32_t)y >=
            surface->height)
    {
        return;
    }

    uint8_t *row =
        (uint8_t *)surface->pixels +
        (size_t)(uint32_t)y *
            surface->pitch;

    gui_color_t *pixel =
        (gui_color_t *)row +
        (uint32_t)x;

    *pixel =
        color;
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
        *result =
            second;

        return !gui_rect_is_empty(
            second);
    }

    if (gui_rect_is_empty(second))
    {
        *result =
            first;

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