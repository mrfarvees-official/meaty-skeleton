#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/mouse_cursor.h>
#include <kernel/framebuffer.h>
#include <kernel/mouse.h>
#include <kernel/paging.h>
#include <kernel/spinlock.h>
#include <kernel/gui/input.h>

/*
 * ------------------------------------------------------------
 * First software mouse cursor
 * ------------------------------------------------------------
 *
 * This layer consumes physical mouse events and owns only:
 *
 *     - absolute framebuffer position
 *     - framebuffer edge clamping
 *     - cursor save/restore
 *     - cursor rendering
 *
 * It intentionally does NOT implement:
 *
 *     - click semantics
 *     - double click
 *     - drag
 *     - acceleration
 *     - wheel input
 *     - windows
 *     - GUI/compositor behavior
 *
 * The low-level PS/2 driver remains physical-event-only.
 */

#define MOUSE_CURSOR_WIDTH 12u
#define MOUSE_CURSOR_HEIGHT 18u

/*
 * Cursor pixels:
 *
 *     '.' transparent
 *     'B' black outline
 *     'W' white fill
 *
 * The cursor hotspot is its top-left pixel.
 */
static const char
    mouse_cursor_shape[MOUSE_CURSOR_HEIGHT][MOUSE_CURSOR_WIDTH + 1u] =
        {
            "B...........",
            "BB..........",
            "BWB.........",
            "BWWB........",
            "BWWWB.......",
            "BWWWWB......",
            "BWWWWWB.....",
            "BWWWWWWB....",
            "BWWWWWWWB...",
            "BWWWWBBBB...",
            "BWWBWB......",
            "BWB.BWB.....",
            "BB..BWB.....",
            "B....BWB....",
            ".....BWB....",
            ".....BWB....",
            ".....BB.....",
            "............"};

static spinlock_t mouse_cursor_lock =
    SPINLOCK_INITIALIZER;

static bool mouse_cursor_initialized;
static bool mouse_cursor_visible;

/*
 * Number of framebuffer updates currently in progress.
 *
 * While nonzero the software cursor remains hidden. This prevents
 * stale saved pixels from being restored over newly rendered terminal
 * contents.
 */
static size_t mouse_cursor_framebuffer_updates;

/*
 * Absolute cursor hotspot position.
 */
static uint32_t mouse_cursor_x;
static uint32_t mouse_cursor_y;

/*
 * Saved framebuffer pixels underneath the currently visible cursor.
 *
 * Pixels are kept in the framebuffer's native packed 32-bit format.
 * This means restoring them is exact and does not require RGB
 * round-tripping.
 */
static uint32_t
    mouse_cursor_backing[MOUSE_CURSOR_HEIGHT]
                        [MOUSE_CURSOR_WIDTH];

static uint32_t mouse_cursor_backing_width;
static uint32_t mouse_cursor_backing_height;

static uint32_t mouse_cursor_backing_x;
static uint32_t mouse_cursor_backing_y;

static uint32_t mouse_cursor_backing_pitch;

static volatile uint8_t *
    mouse_cursor_backing_base;

/*
 * ------------------------------------------------------------
 * Raw framebuffer access
 * ------------------------------------------------------------
 *
 * framebuffer.c permanently maps the physical framebuffer beginning at
 * FRAMEBUFFER_VIRTUAL_BASE plus the physical page offset.
 *
 * We use raw access only for exact save/restore.
 *
 * Cursor drawing itself still goes through framebuffer_put_pixel().
 */

static volatile uint8_t *
mouse_cursor_framebuffer_base(void)
{
    uintptr_t physical_address =
        framebuffer_get_physical_address();

    uintptr_t physical_offset =
        physical_address &
        (PAGE_SIZE - 1u);

    return (volatile uint8_t *)(uintptr_t)(FRAMEBUFFER_VIRTUAL_BASE +
                                           physical_offset);
}

static volatile uint32_t *
mouse_cursor_raw_pixel(
    volatile uint8_t *base,
    uint32_t pitch,
    uint32_t x,
    uint32_t y)
{
    return (volatile uint32_t *)(base +
                                 (uintptr_t)y * pitch +
                                 (uintptr_t)x * 4u);
}

/*
 * ------------------------------------------------------------
 * Cursor backing store
 * ------------------------------------------------------------
 */

static void mouse_cursor_restore_locked(void)
{
    if (!mouse_cursor_visible)
        return;

    if (mouse_cursor_backing_base == NULL)
    {
        mouse_cursor_visible =
            false;

        return;
    }

    for (uint32_t y = 0u;
         y < mouse_cursor_backing_height;
         ++y)
    {
        for (uint32_t x = 0u;
             x < mouse_cursor_backing_width;
             ++x)
        {
            volatile uint32_t *pixel =
                mouse_cursor_raw_pixel(
                    mouse_cursor_backing_base,
                    mouse_cursor_backing_pitch,
                    mouse_cursor_backing_x + x,
                    mouse_cursor_backing_y + y);

            *pixel =
                mouse_cursor_backing[y][x];
        }
    }

    mouse_cursor_visible =
        false;
}

static void mouse_cursor_capture_locked(void)
{
    mouse_cursor_backing_width =
        0u;

    mouse_cursor_backing_height =
        0u;

    mouse_cursor_backing_base =
        NULL;

    if (!framebuffer_is_available())
        return;

    if (framebuffer_get_bpp() != 32u)
        return;

    uint32_t framebuffer_width =
        framebuffer_get_width();

    uint32_t framebuffer_height =
        framebuffer_get_height();

    if (framebuffer_width == 0u ||
        framebuffer_height == 0u)
    {
        return;
    }

    if (mouse_cursor_x >=
            framebuffer_width ||
        mouse_cursor_y >=
            framebuffer_height)
    {
        return;
    }

    uint32_t available_width =
        framebuffer_width -
        mouse_cursor_x;

    uint32_t available_height =
        framebuffer_height -
        mouse_cursor_y;

    mouse_cursor_backing_width =
        available_width <
                MOUSE_CURSOR_WIDTH
            ? available_width
            : MOUSE_CURSOR_WIDTH;

    mouse_cursor_backing_height =
        available_height <
                MOUSE_CURSOR_HEIGHT
            ? available_height
            : MOUSE_CURSOR_HEIGHT;

    mouse_cursor_backing_x =
        mouse_cursor_x;

    mouse_cursor_backing_y =
        mouse_cursor_y;

    mouse_cursor_backing_pitch =
        framebuffer_get_pitch();

    mouse_cursor_backing_base =
        mouse_cursor_framebuffer_base();

    for (uint32_t y = 0u;
         y < mouse_cursor_backing_height;
         ++y)
    {
        for (uint32_t x = 0u;
             x < mouse_cursor_backing_width;
             ++x)
        {
            volatile uint32_t *pixel =
                mouse_cursor_raw_pixel(
                    mouse_cursor_backing_base,
                    mouse_cursor_backing_pitch,
                    mouse_cursor_backing_x + x,
                    mouse_cursor_backing_y + y);

            mouse_cursor_backing[y][x] =
                *pixel;
        }
    }
}

/*
 * ------------------------------------------------------------
 * Rendering
 * ------------------------------------------------------------
 */

static void mouse_cursor_draw_locked(void)
{
    if (!mouse_cursor_initialized)
        return;

    if (mouse_cursor_framebuffer_updates != 0u)
        return;

    if (!framebuffer_is_available())
        return;

    if (framebuffer_get_bpp() != 32u)
        return;

    mouse_cursor_capture_locked();

    if (mouse_cursor_backing_base == NULL)
        return;

    for (uint32_t y = 0u;
         y < mouse_cursor_backing_height;
         ++y)
    {
        for (uint32_t x = 0u;
             x < mouse_cursor_backing_width;
             ++x)
        {
            char pixel =
                mouse_cursor_shape[y][x];

            if (pixel == '.')
                continue;

            uint32_t color =
                pixel == 'W'
                    ? 0xFFFFFFu
                    : 0x000000u;

            framebuffer_put_pixel(
                mouse_cursor_x + x,
                mouse_cursor_y + y,
                color);
        }
    }

    mouse_cursor_visible =
        true;
}

/*
 * ------------------------------------------------------------
 * Position
 * ------------------------------------------------------------
 */

static void mouse_cursor_apply_movement_locked(
    int64_t dx,
    int64_t dy)
{
    uint32_t width =
        framebuffer_get_width();

    uint32_t height =
        framebuffer_get_height();

    if (width == 0u ||
        height == 0u)
    {
        return;
    }

    /*
     * Restore the exact pixels beneath the old position before
     * changing the hotspot.
     */
    mouse_cursor_restore_locked();

    int64_t new_x =
        (int64_t)mouse_cursor_x +
        dx;

    int64_t new_y =
        (int64_t)mouse_cursor_y +
        dy;

    if (new_x < 0)
        new_x = 0;

    if (new_y < 0)
        new_y = 0;

    int64_t maximum_x =
        (int64_t)width - 1;

    int64_t maximum_y =
        (int64_t)height - 1;

    if (new_x > maximum_x)
        new_x = maximum_x;

    if (new_y > maximum_y)
        new_y = maximum_y;

    mouse_cursor_x =
        (uint32_t)new_x;

    mouse_cursor_y =
        (uint32_t)new_y;

    /*
     * If terminal/framebuffer rendering is currently active, leave
     * the cursor hidden. The outer framebuffer-update completion will
     * redraw it over the finished framebuffer contents.
     */
    if (mouse_cursor_framebuffer_updates == 0u)
    {
        mouse_cursor_draw_locked();
    }
}

/*
 * ------------------------------------------------------------
 * Public initialization
 * ------------------------------------------------------------
 */

bool mouse_cursor_initialize(void)
{
    uint32_t flags =
        spin_lock_irqsave(
            &mouse_cursor_lock);

    if (mouse_cursor_initialized)
    {
        spin_unlock_irqrestore(
            &mouse_cursor_lock,
            flags);

        return true;
    }

    if (!mouse_is_initialized() ||
        !framebuffer_is_available() ||
        framebuffer_get_bpp() != 32u)
    {
        spin_unlock_irqrestore(
            &mouse_cursor_lock,
            flags);

        return false;
    }

    uint32_t width =
        framebuffer_get_width();

    uint32_t height =
        framebuffer_get_height();

    if (width == 0u ||
        height == 0u)
    {
        spin_unlock_irqrestore(
            &mouse_cursor_lock,
            flags);

        return false;
    }

    mouse_cursor_x =
        width / 2u;

    mouse_cursor_y =
        height / 2u;

    mouse_cursor_backing_width =
        0u;

    mouse_cursor_backing_height =
        0u;

    mouse_cursor_backing_base =
        NULL;

    mouse_cursor_visible =
        false;

    mouse_cursor_framebuffer_updates =
        0u;

    mouse_cursor_initialized =
        true;

    mouse_cursor_draw_locked();

    spin_unlock_irqrestore(
        &mouse_cursor_lock,
        flags);

    return true;
}

/*
 * ------------------------------------------------------------
 * Mouse event consumer
 * ------------------------------------------------------------
 */

/*
 * ------------------------------------------------------------
 * Mouse event consumer
 * ------------------------------------------------------------
 */

static void mouse_cursor_flush_movement(
    int64_t *dx,
    int64_t *dy,
    uint8_t buttons)
{
    if (dx == NULL ||
        dy == NULL)
    {
        return;
    }

    if (*dx == 0 &&
        *dy == 0)
    {
        return;
    }

    uint32_t flags =
        spin_lock_irqsave(
            &mouse_cursor_lock);

    mouse_cursor_apply_movement_locked(
        *dx,
        *dy);

    int32_t x =
        (int32_t)mouse_cursor_x;

    int32_t y =
        (int32_t)mouse_cursor_y;

    spin_unlock_irqrestore(
        &mouse_cursor_lock,
        flags);

    *dx = 0;
    *dy = 0;

    gui_input_publish_mouse(
        GUI_INPUT_EVENT_MOUSE_MOVE,
        x,
        y,
        MOUSE_BUTTON_LEFT,
        buttons);
}

void mouse_cursor_poll(void)
{
    if (!mouse_cursor_initialized)
        return;

    int64_t total_dx = 0;
    int64_t total_dy = 0;

    uint8_t current_buttons =
        mouse_get_buttons();

    mouse_event_t event;

    while (mouse_read_event(
        &event))
    {
        if (event.type ==
            MOUSE_EVENT_MOVE)
        {
            total_dx +=
                event.dx;

            total_dy +=
                event.dy;

            current_buttons =
                event.buttons;

            continue;
        }

        /*
         * A button transition must occur at the cursor position that
         * existed at that exact point in the physical event stream.
         *
         * Therefore flush all movement preceding the button event
         * before publishing the click.
         */
        mouse_cursor_flush_movement(
            &total_dx,
            &total_dy,
            current_buttons);

        uint32_t flags =
            spin_lock_irqsave(
                &mouse_cursor_lock);

        int32_t x =
            (int32_t)mouse_cursor_x;

        int32_t y =
            (int32_t)mouse_cursor_y;

        spin_unlock_irqrestore(
            &mouse_cursor_lock,
            flags);

        current_buttons =
            event.buttons;

        if (event.type ==
            MOUSE_EVENT_BUTTON_DOWN)
        {
            gui_input_publish_mouse(
                GUI_INPUT_EVENT_MOUSE_BUTTON_DOWN,
                x,
                y,
                event.button,
                event.buttons);
        }
        else if (event.type ==
                 MOUSE_EVENT_BUTTON_UP)
        {
            gui_input_publish_mouse(
                GUI_INPUT_EVENT_MOUSE_BUTTON_UP,
                x,
                y,
                event.button,
                event.buttons);
        }
    }

    mouse_cursor_flush_movement(
        &total_dx,
        &total_dy,
        current_buttons);
}

/*
 * ------------------------------------------------------------
 * Framebuffer writer coordination
 * ------------------------------------------------------------
 *
 * Runtime terminal rendering calls these around one logical terminal
 * character update.
 *
 * IMPORTANT:
 *
 * We do NOT keep mouse_cursor_lock held while framebuffer rendering is
 * happening. Scrolling may redraw the complete terminal and holding an
 * irq-saving spinlock across that work would unnecessarily delay IRQ1,
 * IRQ12 and PIT interrupts.
 *
 * Instead:
 *
 *     begin:
 *         mark framebuffer busy
 *         restore/hide cursor
 *         release lock
 *
 *     renderer:
 *         update framebuffer normally
 *
 *     end:
 *         reacquire lock
 *         mark framebuffer idle
 *         capture fresh pixels
 *         redraw cursor
 */

void mouse_cursor_begin_framebuffer_update(void)
{
    uint32_t flags =
        spin_lock_irqsave(
            &mouse_cursor_lock);

    ++mouse_cursor_framebuffer_updates;

    if (mouse_cursor_visible)
    {
        mouse_cursor_restore_locked();
    }

    spin_unlock_irqrestore(
        &mouse_cursor_lock,
        flags);
}

void mouse_cursor_end_framebuffer_update(void)
{
    uint32_t flags =
        spin_lock_irqsave(
            &mouse_cursor_lock);

    if (mouse_cursor_framebuffer_updates != 0u)
    {
        --mouse_cursor_framebuffer_updates;
    }

    if (mouse_cursor_initialized &&
        mouse_cursor_framebuffer_updates == 0u &&
        !mouse_cursor_visible)
    {
        mouse_cursor_draw_locked();
    }

    spin_unlock_irqrestore(
        &mouse_cursor_lock,
        flags);
}