#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/gui/desktop.h>
#include <kernel/gui/input.h>
#include <kernel/gui/window.h>
#include <kernel/gui/topbar.h>
#include <kernel/gui/taskbar.h>
#include <kernel/semaphore.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>


#define GUI_INPUT_EVENT_BUFFER_SIZE \
    128u


_Static_assert(
    (GUI_INPUT_EVENT_BUFFER_SIZE &
     (GUI_INPUT_EVENT_BUFFER_SIZE - 1u)) == 0u,
    "GUI input buffer size must be a power of two");


static gui_input_event_t
    gui_input_events[
        GUI_INPUT_EVENT_BUFFER_SIZE];

static size_t
    gui_input_read_index;

static size_t
    gui_input_write_index;

static uint32_t
    gui_input_dropped_events;


static spinlock_t gui_input_lock =
    SPINLOCK_INITIALIZER;

static semaphore_t
    gui_input_available;


static bool
    gui_input_initialized;

static task_t *
    gui_input_task;


/*
 * ------------------------------------------------------------
 * Queue
 * ------------------------------------------------------------
 */

static size_t gui_input_next_index(
    size_t index)
{
    return
        (index + 1u) &
        (GUI_INPUT_EVENT_BUFFER_SIZE - 1u);
}


static bool gui_input_push(
    const gui_input_event_t *event)
{
    if (!gui_input_initialized ||
        event == NULL)
    {
        return false;
    }

    uint32_t flags =
        spin_lock_irqsave(
            &gui_input_lock);

    size_t next =
        gui_input_next_index(
            gui_input_write_index);

    if (next ==
        gui_input_read_index)
    {
        ++gui_input_dropped_events;

        spin_unlock_irqrestore(
            &gui_input_lock,
            flags);

        return false;
    }

    gui_input_events[
        gui_input_write_index] =
            *event;

    gui_input_write_index =
        next;

    spin_unlock_irqrestore(
        &gui_input_lock,
        flags);

    /*
     * semaphore_signal() is already used by the keyboard subsystem
     * from IRQ context, so this queue follows the same model.
     */
    semaphore_signal(
        &gui_input_available);

    return true;
}


static bool gui_input_pop(
    gui_input_event_t *event)
{
    if (event == NULL)
        return false;

    uint32_t flags =
        spin_lock_irqsave(
            &gui_input_lock);

    if (gui_input_read_index ==
        gui_input_write_index)
    {
        spin_unlock_irqrestore(
            &gui_input_lock,
            flags);

        return false;
    }

    *event =
        gui_input_events[
            gui_input_read_index];

    gui_input_read_index =
        gui_input_next_index(
            gui_input_read_index);

    spin_unlock_irqrestore(
        &gui_input_lock,
        flags);

    return true;
}


/*
 * ------------------------------------------------------------
 * Keyboard shortcut classification
 * ------------------------------------------------------------
 */

static bool gui_input_alt_active(
    const keyboard_modifiers_t *modifiers)
{
    if (modifiers == NULL)
        return false;

    return
        modifiers->left_alt ||
        modifiers->right_alt;
}


static bool gui_input_shift_active(
    const keyboard_modifiers_t *modifiers)
{
    if (modifiers == NULL)
        return false;

    return
        modifiers->left_shift ||
        modifiers->right_shift;
}


static bool gui_input_is_global_shortcut(
    const keyboard_event_t *event)
{
    if (event == NULL)
        return false;

    /*
     * Alt+Tab / Alt+Shift+Tab are owned by the desktop shell.
     *
     * Consume both DOWN and UP for Tab while Alt remains held so
     * terminal input never receives a stray tab character.
     */
    if (event->key == KEY_TAB &&
        gui_input_alt_active(
            &event->modifiers))
    {
        return true;
    }

    /*
     * Alt+F4 is reserved now even though application/window close
     * lifecycle will be implemented in a later milestone.
     */
    if (event->key == KEY_F4 &&
        gui_input_alt_active(
            &event->modifiers))
    {
        return true;
    }

    return false;
}


/*
 * ------------------------------------------------------------
 * Physical input publishers
 * ------------------------------------------------------------
 */

bool gui_input_filter_keyboard_event(
    const keyboard_event_t *event)
{
    if (event == NULL)
        return false;

    bool consumed =
        gui_input_is_global_shortcut(
            event);

    if (!gui_input_initialized)
        return consumed;

    gui_input_event_t gui_event;

    gui_event.type =
        event->pressed
            ? GUI_INPUT_EVENT_KEY_DOWN
            : GUI_INPUT_EVENT_KEY_UP;

    gui_event.mouse_x = 0;
    gui_event.mouse_y = 0;

    gui_event.mouse_button =
        MOUSE_BUTTON_LEFT;

    gui_event.mouse_buttons = 0u;

    gui_event.key =
        event->key;

    gui_event.modifiers =
        event->modifiers;

    (void)gui_input_push(
        &gui_event);

    return consumed;
}


void gui_input_publish_mouse(
    gui_input_event_type_t type,
    int32_t x,
    int32_t y,
    mouse_button_t button,
    uint8_t buttons)
{
    if (!gui_input_initialized)
        return;

    if (type !=
            GUI_INPUT_EVENT_MOUSE_MOVE &&
        type !=
            GUI_INPUT_EVENT_MOUSE_BUTTON_DOWN &&
        type !=
            GUI_INPUT_EVENT_MOUSE_BUTTON_UP)
    {
        return;
    }

    gui_input_event_t event;

    event.type =
        type;

    event.mouse_x =
        x;

    event.mouse_y =
        y;

    event.mouse_button =
        button;

    event.mouse_buttons =
        buttons;

    event.key =
        KEY_UNKNOWN;

    keyboard_modifiers_t modifiers =
        {0};

    event.modifiers =
        modifiers;

    (void)gui_input_push(
        &event);
}


/*
 * ------------------------------------------------------------
 * Desktop/window dispatch
 * ------------------------------------------------------------
 */

static void gui_input_dispatch_keyboard(
    const gui_input_event_t *event)
{
    if (event == NULL ||
        event->type !=
            GUI_INPUT_EVENT_KEY_DOWN)
    {
        return;
    }

    bool alt =
        gui_input_alt_active(
            &event->modifiers);

    if (!alt)
        return;

    if (event->key ==
        KEY_TAB)
    {
        bool changed;

        if (gui_input_shift_active(
                &event->modifiers))
        {
            changed =
                gui_window_focus_previous();
        }
        else
        {
            changed =
                gui_window_focus_next();
        }

        if (changed)
            gui_desktop_render();

        return;
    }

    /*
     * Alt+F4 is intentionally reserved but does not destroy a
     * window yet.
     *
     * Application/window lifetime needs its own focused milestone.
     */
}


static void gui_input_dispatch_mouse(
    const gui_input_event_t *event)
{
    if (event == NULL)
        return;

    /*
     * System chrome receives input before application windows.
     *
     * The topbar owns:
     *
     *     - Meaty OS / Start
     *     - system menus
     *     - power controls
     */
    if (gui_topbar_handle_pointer(
            event->type,
            event->mouse_x,
            event->mouse_y,
            event->mouse_button))
    {
        return;
    }

    /*
     * The bottom dock is intentionally application-only.
     *
     * It has no controls yet, therefore it needs no pointer
     * dispatcher at this milestone.
     */

    if (event->type !=
            GUI_INPUT_EVENT_MOUSE_BUTTON_DOWN ||
        event->mouse_button !=
            MOUSE_BUTTON_LEFT)
    {
        return;
    }

    if (gui_window_focus_at_point(
            event->mouse_x,
            event->mouse_y))
    {
        gui_desktop_render();
    }
}


static void gui_input_dispatch(
    const gui_input_event_t *event)
{
    if (event == NULL)
        return;

    switch (event->type)
    {
    case GUI_INPUT_EVENT_MOUSE_BUTTON_DOWN:
    case GUI_INPUT_EVENT_MOUSE_BUTTON_UP:
    case GUI_INPUT_EVENT_MOUSE_MOVE:

        gui_input_dispatch_mouse(
            event);

        break;

    case GUI_INPUT_EVENT_KEY_DOWN:
    case GUI_INPUT_EVENT_KEY_UP:

        gui_input_dispatch_keyboard(
            event);

        break;

    default:
        break;
    }
}


/*
 * ------------------------------------------------------------
 * Dispatcher task
 * ------------------------------------------------------------
 */

static void gui_input_thread(
    void *argument)
{
    (void)argument;

    for (;;)
    {
        if (!semaphore_wait(
                &gui_input_available))
        {
            task_yield();
            continue;
        }

        gui_input_event_t event;

        if (!gui_input_pop(
                &event))
        {
            continue;
        }

        gui_input_dispatch(
            &event);
    }
}


/*
 * ------------------------------------------------------------
 * Initialization
 * ------------------------------------------------------------
 */

bool gui_input_initialize(void)
{
    if (gui_input_initialized)
        return true;

    gui_input_read_index = 0u;
    gui_input_write_index = 0u;

    gui_input_dropped_events = 0u;

    semaphore_initialize(
        &gui_input_available,
        0u);

    gui_input_initialized =
        true;

    gui_input_task =
        task_create_kernel(
            gui_input_thread,
            NULL);

    if (gui_input_task == NULL)
    {
        gui_input_initialized =
            false;

        return false;
    }

    return true;
}

