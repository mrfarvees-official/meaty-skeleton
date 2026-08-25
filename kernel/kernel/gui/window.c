#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/window.h>


bool gui_window_create(
    gui_window_t *window,
    int32_t x,
    int32_t y,
    uint32_t width,
    uint32_t height,
    gui_z_class_t z_class)
{
    if (window == NULL ||
        width == 0u ||
        height == 0u)
        return false;

    memset(window, 0, sizeof(*window));

    if (!gui_surface_create(
            &window->surface,
            width,
            height))
        return false;

    window->x = x;
    window->y = y;
    window->z_class = z_class;
    window->visible = true;

    return true;
}

void gui_window_destroy(gui_window_t *window)
{
    if (window == NULL)
        return;

    gui_surface_destroy(&window->surface);

    memset(window, 0, sizeof(*window));
}

gui_surface_t *gui_window_surface(gui_window_t *window)
{
    if (window == NULL)
        return NULL;

    return &window->surface;
}

gui_rect_t gui_window_bounds(const gui_window_t *window)
{
    gui_rect_t result;

    result.x = 0;
    result.y = 0;
    result.width = 0u;
    result.height = 0u;

    if (window == NULL)
        return result;

    result.x = window->x;
    result.y = window->y;
    result.width = window->surface.width;
    result.height = window->surface.height;

    return result;
}

void gui_window_set_visible(gui_window_t *window, bool visible)
{
    if (window == NULL)
        return;

    window->visible = visible;
}

void gui_window_composite(const gui_window_t *window)
{
    if (window == NULL ||
        !window->visible ||
        window->surface.pixels == NULL)
        return;

    if (!gui_compositor_is_initialized())
        return;

    gui_surface_t *destination = gui_compositor_surface();

    if (destination == NULL || 
        destination->pixels == NULL)
        return;

    // Window bounds are in screen coordinates.
    gui_rect_t window_rect = gui_window_bounds(window);

    gui_rect_t screen_rect;

    screen_rect.x = 0;
    screen_rect.y = 0;
    screen_rect.width = destination->width;
    screen_rect.height = destination->height;

    /*
     * Determine the part of the window that is actually visible
     * on-screen.
     */
    gui_rect_t clipped;

    if (!gui_rect_intersect(
            window_rect,
            screen_rect,
            &clipped))
        return;

    /*
     * Translate the clipped screen position back into coordinates
     * inside the window surface.
     *
     * These values are always non-negative because clipped lies
     * within window_rect.
     */
    uint32_t source_x = (uint32_t)((int64_t)clipped.x - (int64_t)window->x);
    uint32_t source_y = (uint32_t)((int64_t)clipped.y - (int64_t)window->y);

    for (uint32_t row = 0u; row < clipped.height; ++row)
    {
        const uint8_t *source_row_bytes = 
            (const uint8_t *)window->surface.pixels + 
                (size_t)(source_y + row) * window->surface.pitch + 
                    (size_t)source_x * sizeof(gui_color_t); 

        uint8_t *destination_row_bytes = 
            (uint8_t *)destination->pixels +
                (size_t)((uint32_t)clipped.y + row) * destination->pitch +
                    (size_t)(uint32_t)clipped.x * sizeof(gui_color_t);

        memcpy(
            destination_row_bytes, 
            source_row_bytes,
            (size_t)clipped.width * sizeof(gui_color_t));
    }

    // Presentation remains compositor-owned
    gui_compositor_damage(clipped);
}