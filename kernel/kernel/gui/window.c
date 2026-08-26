#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/window.h>
#include <kernel/spinlock.h>


#define GUI_NORMAL_WINDOW_CAPACITY \
    64u


static gui_window_t *
    normal_windows[
        GUI_NORMAL_WINDOW_CAPACITY];

static size_t
    normal_window_count;

static gui_window_t *
    active_normal_window;


static spinlock_t window_registry_lock =
    SPINLOCK_INITIALIZER;


/*
 * ------------------------------------------------------------
 * Registry helpers
 * ------------------------------------------------------------
 */

static bool gui_window_registry_add(
    gui_window_t *window)
{
    if (window == NULL ||
        window->z_class !=
            GUI_Z_NORMAL)
    {
        return true;
    }

    uint32_t flags =
        spin_lock_irqsave(
            &window_registry_lock);

    if (normal_window_count >=
        GUI_NORMAL_WINDOW_CAPACITY)
    {
        spin_unlock_irqrestore(
            &window_registry_lock,
            flags);

        return false;
    }

    normal_windows[
        normal_window_count] =
            window;

    ++normal_window_count;

    active_normal_window =
        window;

    spin_unlock_irqrestore(
        &window_registry_lock,
        flags);

    return true;
}


static void gui_window_registry_remove(
    gui_window_t *window)
{
    if (window == NULL)
        return;

    uint32_t flags =
        spin_lock_irqsave(
            &window_registry_lock);

    size_t index =
        normal_window_count;

    for (size_t i = 0u;
         i < normal_window_count;
         ++i)
    {
        if (normal_windows[i] ==
            window)
        {
            index = i;
            break;
        }
    }

    if (index ==
        normal_window_count)
    {
        spin_unlock_irqrestore(
            &window_registry_lock,
            flags);

        return;
    }

    for (size_t i = index;
         i + 1u <
            normal_window_count;
         ++i)
    {
        normal_windows[i] =
            normal_windows[i + 1u];
    }

    --normal_window_count;

    normal_windows[
        normal_window_count] =
            NULL;

    if (active_normal_window ==
        window)
    {
        active_normal_window =
            NULL;

        for (size_t i =
                 normal_window_count;
             i > 0u;
             --i)
        {
            gui_window_t *candidate =
                normal_windows[
                    i - 1u];

            if (candidate != NULL &&
                candidate->visible)
            {
                active_normal_window =
                    candidate;

                break;
            }
        }
    }

    spin_unlock_irqrestore(
        &window_registry_lock,
        flags);
}


static bool gui_window_point_inside(
    const gui_window_t *window,
    int32_t x,
    int32_t y)
{
    if (window == NULL ||
        !window->visible)
    {
        return false;
    }

    gui_rect_t bounds =
        gui_window_bounds(
            window);

    int64_t left =
        bounds.x;

    int64_t top =
        bounds.y;

    int64_t right =
        left +
        (int64_t)bounds.width;

    int64_t bottom =
        top +
        (int64_t)bounds.height;

    return
        (int64_t)x >= left &&
        (int64_t)y >= top &&
        (int64_t)x < right &&
        (int64_t)y < bottom;
}


/*
 * Caller holds window_registry_lock.
 */
static bool gui_window_focus_index_locked(
    size_t index)
{
    if (index >=
        normal_window_count)
    {
        return false;
    }

    gui_window_t *window =
        normal_windows[index];

    if (window == NULL ||
        !window->visible)
    {
        return false;
    }

    if (active_normal_window ==
            window &&
        index + 1u ==
            normal_window_count)
    {
        return false;
    }

    /*
     * Move focused window to the front of the normal stack.
     */
    for (size_t i = index;
         i + 1u <
            normal_window_count;
         ++i)
    {
        normal_windows[i] =
            normal_windows[i + 1u];
    }

    normal_windows[
        normal_window_count - 1u] =
            window;

    active_normal_window =
        window;

    return true;
}


/*
 * ------------------------------------------------------------
 * Window lifecycle
 * ------------------------------------------------------------
 */

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
    {
        return false;
    }

    memset(
        window,
        0,
        sizeof(*window));

    if (!gui_surface_create(
            &window->surface,
            width,
            height))
    {
        return false;
    }

    window->x = x;
    window->y = y;

    window->z_class =
        z_class;

    window->visible =
        true;

    if (!gui_window_registry_add(
            window))
    {
        gui_surface_destroy(
            &window->surface);

        memset(
            window,
            0,
            sizeof(*window));

        return false;
    }

    return true;
}


void gui_window_destroy(
    gui_window_t *window)
{
    if (window == NULL)
        return;

    gui_window_registry_remove(
        window);

    gui_surface_destroy(
        &window->surface);

    memset(
        window,
        0,
        sizeof(*window));
}


gui_surface_t *gui_window_surface(
    gui_window_t *window)
{
    if (window == NULL)
        return NULL;

    return
        &window->surface;
}


gui_rect_t gui_window_bounds(
    const gui_window_t *window)
{
    gui_rect_t result;

    result.x = 0;
    result.y = 0;
    result.width = 0u;
    result.height = 0u;

    if (window == NULL)
        return result;

    result.x =
        window->x;

    result.y =
        window->y;

    result.width =
        window->surface.width;

    result.height =
        window->surface.height;

    return result;
}


void gui_window_set_visible(
    gui_window_t *window,
    bool visible)
{
    if (window == NULL)
        return;

    window->visible =
        visible;

    if (visible ||
        window->z_class !=
            GUI_Z_NORMAL)
    {
        return;
    }

    uint32_t flags =
        spin_lock_irqsave(
            &window_registry_lock);

    if (active_normal_window ==
        window)
    {
        active_normal_window =
            NULL;

        for (size_t i =
                 normal_window_count;
             i > 0u;
             --i)
        {
            gui_window_t *candidate =
                normal_windows[
                    i - 1u];

            if (candidate != NULL &&
                candidate->visible)
            {
                active_normal_window =
                    candidate;

                break;
            }
        }
    }

    spin_unlock_irqrestore(
        &window_registry_lock,
        flags);
}


/*
 * ------------------------------------------------------------
 * Composition
 * ------------------------------------------------------------
 */

void gui_window_composite(
    const gui_window_t *window)
{
    if (window == NULL ||
        !window->visible ||
        window->surface.pixels == NULL)
    {
        return;
    }

    if (!gui_compositor_is_initialized())
        return;

    gui_surface_t *destination =
        gui_compositor_surface();

    if (destination == NULL ||
        destination->pixels == NULL)
    {
        return;
    }

    gui_rect_t window_rect =
        gui_window_bounds(
            window);

    gui_rect_t screen_rect;

    screen_rect.x = 0;
    screen_rect.y = 0;

    screen_rect.width =
        destination->width;

    screen_rect.height =
        destination->height;

    gui_rect_t clipped;

    if (!gui_rect_intersect(
            window_rect,
            screen_rect,
            &clipped))
    {
        return;
    }

    uint32_t source_x =
        (uint32_t)
        ((int64_t)clipped.x -
         (int64_t)window->x);

    uint32_t source_y =
        (uint32_t)
        ((int64_t)clipped.y -
         (int64_t)window->y);

    for (uint32_t row = 0u;
         row < clipped.height;
         ++row)
    {
        const uint8_t *source_row_bytes =
            (const uint8_t *)
                window->surface.pixels +
            (size_t)(source_y + row) *
                window->surface.pitch;

        uint8_t *destination_row_bytes =
            (uint8_t *)
                destination->pixels +
            (size_t)
                ((uint32_t)clipped.y + row) *
                destination->pitch;

        const gui_color_t *source_row =
            (const gui_color_t *)
                source_row_bytes;

        gui_color_t *destination_row =
            (gui_color_t *)
                destination_row_bytes;

        for (uint32_t column = 0u;
             column < clipped.width;
             ++column)
        {
            gui_color_t source_color =
                source_row[
                    source_x +
                    column];

            uint8_t source_alpha =
                gui_color_alpha(
                    source_color);

            if (source_alpha == 0u)
                continue;

            uint32_t destination_x =
                (uint32_t)clipped.x +
                column;

            if (source_alpha == 255u)
            {
                destination_row[
                    destination_x] =
                        source_color;

                continue;
            }

            destination_row[
                destination_x] =
                    gui_color_blend(
                        destination_row[
                            destination_x],
                        source_color);
        }
    }

    gui_compositor_damage(
        clipped);
}


/*
 * ------------------------------------------------------------
 * Focus
 * ------------------------------------------------------------
 */

gui_window_t *gui_window_active(void)
{
    uint32_t flags =
        spin_lock_irqsave(
            &window_registry_lock);

    gui_window_t *result =
        active_normal_window;

    spin_unlock_irqrestore(
        &window_registry_lock,
        flags);

    return result;
}


bool gui_window_focus(
    gui_window_t *window)
{
    if (window == NULL ||
        window->z_class !=
            GUI_Z_NORMAL ||
        !window->visible)
    {
        return false;
    }

    uint32_t flags =
        spin_lock_irqsave(
            &window_registry_lock);

    size_t index =
        normal_window_count;

    for (size_t i = 0u;
         i < normal_window_count;
         ++i)
    {
        if (normal_windows[i] ==
            window)
        {
            index = i;
            break;
        }
    }

    bool changed =
        gui_window_focus_index_locked(
            index);

    spin_unlock_irqrestore(
        &window_registry_lock,
        flags);

    return changed;
}


bool gui_window_focus_at_point(
    int32_t x,
    int32_t y)
{
    uint32_t flags =
        spin_lock_irqsave(
            &window_registry_lock);

    size_t index =
        normal_window_count;

    for (size_t i =
             normal_window_count;
         i > 0u;
         --i)
    {
        gui_window_t *window =
            normal_windows[
                i - 1u];

        if (gui_window_point_inside(
                window,
                x,
                y))
        {
            index =
                i - 1u;

            break;
        }
    }

    bool changed =
        gui_window_focus_index_locked(
            index);

    spin_unlock_irqrestore(
        &window_registry_lock,
        flags);

    return changed;
}


bool gui_window_focus_next(void)
{
    uint32_t flags =
        spin_lock_irqsave(
            &window_registry_lock);

    if (normal_window_count <
        2u)
    {
        spin_unlock_irqrestore(
            &window_registry_lock,
            flags);

        return false;
    }

    size_t active_index =
        normal_window_count;

    for (size_t i = 0u;
         i < normal_window_count;
         ++i)
    {
        if (normal_windows[i] ==
            active_normal_window)
        {
            active_index = i;
            break;
        }
    }

    size_t index =
        active_index;

    for (size_t step = 1u;
         step <=
            normal_window_count;
         ++step)
    {
        size_t candidate =
            active_index ==
                normal_window_count
                ? step - 1u
                : (active_index + step) %
                    normal_window_count;

        gui_window_t *window =
            normal_windows[
                candidate];

        if (window != NULL &&
            window->visible &&
            window !=
                active_normal_window)
        {
            index =
                candidate;

            break;
        }
    }

    bool changed =
        gui_window_focus_index_locked(
            index);

    spin_unlock_irqrestore(
        &window_registry_lock,
        flags);

    return changed;
}


bool gui_window_focus_previous(void)
{
    uint32_t flags =
        spin_lock_irqsave(
            &window_registry_lock);

    if (normal_window_count <
        2u)
    {
        spin_unlock_irqrestore(
            &window_registry_lock,
            flags);

        return false;
    }

    size_t active_index =
        normal_window_count;

    for (size_t i = 0u;
         i < normal_window_count;
         ++i)
    {
        if (normal_windows[i] ==
            active_normal_window)
        {
            active_index = i;
            break;
        }
    }

    size_t index =
        active_index;

    for (size_t step = 1u;
         step <=
            normal_window_count;
         ++step)
    {
        size_t candidate;

        if (active_index ==
            normal_window_count)
        {
            candidate =
                normal_window_count -
                step;
        }
        else
        {
            candidate =
                (active_index +
                 normal_window_count -
                 (step %
                  normal_window_count)) %
                normal_window_count;
        }

        gui_window_t *window =
            normal_windows[
                candidate];

        if (window != NULL &&
            window->visible &&
            window !=
                active_normal_window)
        {
            index =
                candidate;

            break;
        }
    }

    bool changed =
        gui_window_focus_index_locked(
            index);

    spin_unlock_irqrestore(
        &window_registry_lock,
        flags);

    return changed;
}


void gui_window_composite_normal_windows(void)
{
    gui_window_t *
        snapshot[
            GUI_NORMAL_WINDOW_CAPACITY];

    size_t count;

    uint32_t flags =
        spin_lock_irqsave(
            &window_registry_lock);

    count =
        normal_window_count;

    for (size_t i = 0u;
         i < count;
         ++i)
    {
        snapshot[i] =
            normal_windows[i];
    }

    spin_unlock_irqrestore(
        &window_registry_lock,
        flags);

    /*
     * Do not keep an irq-saving spinlock around pixel composition.
     */
    for (size_t i = 0u;
         i < count;
         ++i)
    {
        gui_window_composite(
            snapshot[i]);
    }
}