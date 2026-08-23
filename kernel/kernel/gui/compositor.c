#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/surface.h>
#include <kernel/framebuffer.h>
#include <kernel/mouse_cursor.h>
#include <kernel/spinlock.h>


typedef struct
{
    bool initialized;

    gui_surface_t backbuffer;

    gui_rect_t
        damage[
            GUI_COMPOSITOR_MAX_DAMAGE_RECTS];

    size_t damage_count;

    bool full_damage;

    spinlock_t lock;
} gui_compositor_state_t;


static gui_compositor_state_t compositor =
{
    .lock = SPINLOCK_INITIALIZER
};


static gui_rect_t
gui_compositor_screen_rect(void)
{
    gui_rect_t result;

    result.x = 0;
    result.y = 0;

    result.width =
        compositor.backbuffer.width;

    result.height =
        compositor.backbuffer.height;

    return result;
}


static bool gui_rects_touch_or_overlap(
    gui_rect_t first,
    gui_rect_t second)
{
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

    return
        first_left <= second_right &&
        first_right >= second_left &&
        first_top <= second_bottom &&
        first_bottom >= second_top;
}


bool gui_compositor_initialize(void)
{
    uint32_t width =
        framebuffer_get_width();

    uint32_t height =
        framebuffer_get_height();

    if (!framebuffer_is_available() ||
        framebuffer_get_bpp() != 32u ||
        width == 0u ||
        height == 0u)
    {
        return false;
    }

    uint32_t flags =
        spin_lock_irqsave(
            &compositor.lock);

    if (compositor.initialized)
    {
        bool compatible =
            compositor.backbuffer.width ==
                width &&
            compositor.backbuffer.height ==
                height;

        spin_unlock_irqrestore(
            &compositor.lock,
            flags);

        return compatible;
    }

    /*
     * Allocation must not happen while holding the compositor
     * spinlock because kmalloc() has its own memory lock chain.
     */
    spin_unlock_irqrestore(
        &compositor.lock,
        flags);

    gui_surface_t surface;

    memset(
        &surface,
        0,
        sizeof(surface));

    if (!gui_surface_create(
            &surface,
            width,
            height))
    {
        return false;
    }

    /*
     * Start with a deterministic black composition target.
     *
     * We deliberately do NOT present it during initialization.
     * Existing framebuffer terminal contents therefore remain
     * untouched until the GUI deliberately owns the screen.
     */
    gui_surface_clear(
        &surface,
        GUI_RGB(0, 0, 0));

    flags =
        spin_lock_irqsave(
            &compositor.lock);

    if (compositor.initialized)
    {
        spin_unlock_irqrestore(
            &compositor.lock,
            flags);

        gui_surface_destroy(
            &surface);

        return true;
    }

    compositor.backbuffer =
        surface;

    compositor.damage_count =
        0u;

    compositor.full_damage =
        false;

    compositor.initialized =
        true;

    spin_unlock_irqrestore(
        &compositor.lock,
        flags);

    return true;
}


bool gui_compositor_is_initialized(void)
{
    uint32_t flags =
        spin_lock_irqsave(
            &compositor.lock);

    bool initialized =
        compositor.initialized;

    spin_unlock_irqrestore(
        &compositor.lock,
        flags);

    return initialized;
}


gui_surface_t *
gui_compositor_surface(void)
{
    if (!compositor.initialized)
        return NULL;

    return
        &compositor.backbuffer;
}


void gui_compositor_damage_all(void)
{
    uint32_t flags =
        spin_lock_irqsave(
            &compositor.lock);

    if (!compositor.initialized)
    {
        spin_unlock_irqrestore(
            &compositor.lock,
            flags);

        return;
    }

    compositor.full_damage =
        true;

    compositor.damage_count =
        0u;

    spin_unlock_irqrestore(
        &compositor.lock,
        flags);
}


void gui_compositor_damage(
    gui_rect_t rect)
{
    uint32_t flags =
        spin_lock_irqsave(
            &compositor.lock);

    if (!compositor.initialized ||
        compositor.full_damage)
    {
        spin_unlock_irqrestore(
            &compositor.lock,
            flags);

        return;
    }

    gui_rect_t clipped;

    if (!gui_rect_intersect(
            rect,
            gui_compositor_screen_rect(),
            &clipped))
    {
        spin_unlock_irqrestore(
            &compositor.lock,
            flags);

        return;
    }

    /*
     * Merge repeatedly because combining two rectangles may cause
     * the resulting rectangle to touch another existing one.
     */
    bool merged;

    do
    {
        merged =
            false;

        for (size_t index = 0u;
             index <
                 compositor.damage_count;
             ++index)
        {
            if (!gui_rects_touch_or_overlap(
                    clipped,
                    compositor.damage[index]))
            {
                continue;
            }

            gui_rect_t combined;

            if (!gui_rect_union(
                    clipped,
                    compositor.damage[index],
                    &combined))
            {
                continue;
            }

            clipped =
                combined;

            compositor.damage[index] =
                compositor.damage[
                    compositor.damage_count -
                    1u];

            --compositor.damage_count;

            merged =
                true;

            break;
        }
    }
    while (merged);

    if (compositor.damage_count >=
        GUI_COMPOSITOR_MAX_DAMAGE_RECTS)
    {
        compositor.full_damage =
            true;

        compositor.damage_count =
            0u;

        spin_unlock_irqrestore(
            &compositor.lock,
            flags);

        return;
    }

    compositor.damage[
        compositor.damage_count++] =
            clipped;

    spin_unlock_irqrestore(
        &compositor.lock,
        flags);
}


size_t
gui_compositor_damage_count(void)
{
    uint32_t flags =
        spin_lock_irqsave(
            &compositor.lock);

    size_t count =
        compositor.full_damage
            ? 1u
            : compositor.damage_count;

    spin_unlock_irqrestore(
        &compositor.lock,
        flags);

    return count;
}


void gui_compositor_present(void)
{
    /*
     * Keep the lock only long enough to snapshot and clear the
     * damage state.
     *
     * We do NOT disable interrupts across potentially large
     * framebuffer transfers.
     */
    gui_rect_t
        local_damage[
            GUI_COMPOSITOR_MAX_DAMAGE_RECTS];

    size_t local_count = 0u;
    bool local_full_damage = false;

    uint32_t flags =
        spin_lock_irqsave(
            &compositor.lock);

    if (!compositor.initialized)
    {
        spin_unlock_irqrestore(
            &compositor.lock,
            flags);

        return;
    }

    local_full_damage =
        compositor.full_damage;

    if (!local_full_damage)
    {
        local_count =
            compositor.damage_count;

        for (size_t index = 0u;
             index < local_count;
             ++index)
        {
            local_damage[index] =
                compositor.damage[index];
        }
    }

    compositor.full_damage =
        false;

    compositor.damage_count =
        0u;

    spin_unlock_irqrestore(
        &compositor.lock,
        flags);

    if (!local_full_damage &&
        local_count == 0u)
    {
        return;
    }

    /*
     * Protect the software cursor's saved backing from becoming
     * stale while the compositor changes framebuffer pixels.
     */
    mouse_cursor_begin_framebuffer_update();

    if (local_full_damage)
    {
        gui_rect_t screen =
            gui_compositor_screen_rect();

        framebuffer_blit_rgb32(
            0u,
            0u,
            screen.width,
            screen.height,
            compositor.backbuffer.pixels,
            compositor.backbuffer.pitch);
    }
    else
    {
        for (size_t index = 0u;
             index < local_count;
             ++index)
        {
            gui_rect_t rect =
                local_damage[index];

            const uint8_t *surface_bytes =
                (const uint8_t *)
                compositor.backbuffer.pixels;

            const uint32_t *source =
                (const uint32_t *)
                (surface_bytes +
                 (size_t)(uint32_t)rect.y *
                     compositor.backbuffer.pitch +
                 (size_t)(uint32_t)rect.x *
                     sizeof(uint32_t));

            framebuffer_blit_rgb32(
                (uint32_t)rect.x,
                (uint32_t)rect.y,
                rect.width,
                rect.height,
                source,
                compositor.backbuffer.pitch);
        }
    }

    mouse_cursor_end_framebuffer_update();
}