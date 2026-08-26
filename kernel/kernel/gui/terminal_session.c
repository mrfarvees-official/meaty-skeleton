#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/desktop.h>
#include <kernel/gui/font.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/terminal_session.h>
#include <kernel/gui/window.h>

#include <kernel/process.h>
#include <kernel/spawn.h>
#include <kernel/spinlock.h>

#define GUI_TERMINAL_MAX_SESSIONS \
    4u

#define GUI_TERMINAL_COLUMNS \
    80u

#define GUI_TERMINAL_ROWS \
    24u

/*
 * Complete terminal history retained per session.
 *
 * 1024 x 80 = 81920 characters per session.
 *
 * This is deliberately fixed-size and allocation-free after
 * session creation.
 */
#define GUI_TERMINAL_HISTORY_LINES \
    1024u

#define GUI_TERMINAL_INPUT_CAPACITY \
    256u

#define GUI_TERMINAL_WINDOW_WIDTH \
    700u

#define GUI_TERMINAL_WINDOW_HEIGHT \
    440u

#define GUI_TERMINAL_TITLE_HEIGHT \
    34u

#define GUI_TERMINAL_PADDING_X \
    14

#define GUI_TERMINAL_PADDING_Y \
    10

#define GUI_TERMINAL_FONT_HEIGHT \
    15u

#define GUI_TERMINAL_LINE_HEIGHT \
    16

#define GUI_TERMINAL_START_X \
    120

#define GUI_TERMINAL_START_Y \
    90

#define GUI_TERMINAL_CASCADE \
    28

/*
 * Keyboard PageUp/PageDown moves almost one complete viewport.
 */
#define GUI_TERMINAL_PAGE_SCROLL \
    (GUI_TERMINAL_ROWS - 2u)

typedef struct gui_terminal_cell
{
    char character;

} gui_terminal_cell_t;

struct gui_terminal_session
{
    bool used;

    process_id_t root_pid;

    gui_window_t window;

    /*
     * Scrollback ring.
     *
     * history_start:
     *     physical index of oldest logical line.
     *
     * history_count:
     *     number of valid logical lines.
     *
     * The newest logical line is always the active output line.
     */
    gui_terminal_cell_t
        history[GUI_TERMINAL_HISTORY_LINES]
               [GUI_TERMINAL_COLUMNS];

    size_t history_start;
    size_t history_count;

    /*
     * Current output position inside the newest history line.
     */
    size_t column;

    /*
     * Number of lines viewport is displaced upward from newest
     * possible position.
     *
     *     0 = follow bottom
     *     1 = one line older
     *     ...
     */
    size_t scroll_offset;

    char input[GUI_TERMINAL_INPUT_CAPACITY];

    size_t input_read;
    size_t input_write;

    spinlock_t lock;
};

static gui_terminal_session_t
    terminal_sessions[GUI_TERMINAL_MAX_SESSIONS];

/*
 * ------------------------------------------------------------
 * Input queue
 * ------------------------------------------------------------
 */

static size_t gui_terminal_input_next(
    size_t index)
{
    return (index + 1u) %
           GUI_TERMINAL_INPUT_CAPACITY;
}

static bool gui_terminal_input_push(
    gui_terminal_session_t *session,
    char character)
{
    if (session == NULL ||
        character == '\0')
    {
        return false;
    }

    uint32_t flags =
        spin_lock_irqsave(
            &session->lock);

    size_t next =
        gui_terminal_input_next(
            session->input_write);

    if (next ==
        session->input_read)
    {
        spin_unlock_irqrestore(
            &session->lock,
            flags);

        return false;
    }

    session->input[session->input_write] =
        character;

    session->input_write =
        next;

    spin_unlock_irqrestore(
        &session->lock,
        flags);

    return true;
}

static void gui_terminal_input_sequence(
    gui_terminal_session_t *session,
    const char *sequence)
{
    if (session == NULL ||
        sequence == NULL)
    {
        return;
    }

    while (*sequence != '\0')
    {
        (void)
            gui_terminal_input_push(
                session,
                *sequence);

        ++sequence;
    }
}

size_t gui_terminal_session_read(
    gui_terminal_session_t *session,
    char *buffer,
    size_t capacity)
{
    if (session == NULL ||
        buffer == NULL ||
        capacity == 0u)
    {
        return 0u;
    }

    uint32_t flags =
        spin_lock_irqsave(
            &session->lock);

    size_t received =
        0u;

    while (received <
               capacity &&
           session->input_read !=
               session->input_write)
    {
        buffer[received++] =
            session->input[session->input_read];

        session->input_read =
            gui_terminal_input_next(
                session->input_read);
    }

    spin_unlock_irqrestore(
        &session->lock,
        flags);

    return received;
}

/*
 * ------------------------------------------------------------
 * Scrollback/history helpers
 * ------------------------------------------------------------
 */

static size_t gui_terminal_logical_to_physical(
    const gui_terminal_session_t *session,
    size_t logical_line)
{
    if (session == NULL ||
        logical_line >=
            session->history_count)
    {
        return 0u;
    }

    return (session->history_start +
            logical_line) %
           GUI_TERMINAL_HISTORY_LINES;
}

static size_t gui_terminal_active_line(
    const gui_terminal_session_t *session)
{
    if (session == NULL ||
        session->history_count == 0u)
    {
        return 0u;
    }

    return gui_terminal_logical_to_physical(
        session,
        session->history_count -
            1u);
}

static void gui_terminal_clear_physical_line(
    gui_terminal_session_t *session,
    size_t physical_line)
{
    if (session == NULL ||
        physical_line >=
            GUI_TERMINAL_HISTORY_LINES)
    {
        return;
    }

    for (size_t column = 0u;
         column <
         GUI_TERMINAL_COLUMNS;
         ++column)
    {
        session->history[physical_line]
                        [column]
                            .character =
            ' ';
    }
}

static size_t gui_terminal_max_scroll_offset(
    const gui_terminal_session_t *session)
{
    if (session == NULL ||
        session->history_count <=
            GUI_TERMINAL_ROWS)
    {
        return 0u;
    }

    return session->history_count -
           GUI_TERMINAL_ROWS;
}

static void gui_terminal_clamp_scroll_offset(
    gui_terminal_session_t *session)
{
    if (session == NULL)
        return;

    size_t maximum =
        gui_terminal_max_scroll_offset(
            session);

    if (session->scroll_offset >
        maximum)
    {
        session->scroll_offset =
            maximum;
    }
}

static void gui_terminal_reset_history_locked(
    gui_terminal_session_t *session)
{
    if (session == NULL)
        return;

    session->history_start =
        0u;

    session->history_count =
        1u;

    session->column =
        0u;

    session->scroll_offset =
        0u;

    /*
     * Old ring contents become unreachable immediately.
     *
     * Only clear the one newly visible active line rather than
     * performing an 80 KiB clear while the spinlock is held.
     */
    gui_terminal_clear_physical_line(
        session,
        0u);
}

/*
 * Append one empty logical line.
 *
 * The newest line is always the active line.
 */
static void gui_terminal_append_line_locked(
    gui_terminal_session_t *session)
{
    if (session == NULL)
        return;

    bool was_scrolled =
        session->scroll_offset != 0u;

    if (session->history_count <
        GUI_TERMINAL_HISTORY_LINES)
    {
        size_t physical =
            (session->history_start +
             session->history_count) %
            GUI_TERMINAL_HISTORY_LINES;

        ++session->history_count;

        gui_terminal_clear_physical_line(
            session,
            physical);
    }
    else
    {
        /*
         * Ring is full.
         *
         * Discard the oldest line and reuse its physical slot as
         * the new newest line.
         */
        session->history_start =
            (session->history_start +
             1u) %
            GUI_TERMINAL_HISTORY_LINES;

        size_t physical =
            gui_terminal_active_line(
                session);

        gui_terminal_clear_physical_line(
            session,
            physical);
    }

    /*
     * If the user is reading historical output, new output must
     * not snap their viewport back to the bottom.
     *
     * Moving the bottom one line downward means the offset must
     * also move one line upward to keep roughly the same content
     * visible.
     */
    if (was_scrolled &&
        session->scroll_offset <
            GUI_TERMINAL_HISTORY_LINES)
    {
        ++session->scroll_offset;
    }

    gui_terminal_clamp_scroll_offset(
        session);
}

static void gui_terminal_newline_locked(
    gui_terminal_session_t *session)
{
    if (session == NULL)
        return;

    session->column =
        0u;

    gui_terminal_append_line_locked(
        session);
}

/*
 * ------------------------------------------------------------
 * Viewport scrolling
 * ------------------------------------------------------------
 */

static bool gui_terminal_scroll_view_up(
    gui_terminal_session_t *session,
    size_t lines)
{
    if (session == NULL ||
        lines == 0u)
    {
        return false;
    }

    uint32_t flags =
        spin_lock_irqsave(
            &session->lock);

    size_t maximum =
        gui_terminal_max_scroll_offset(
            session);

    size_t old_offset =
        session->scroll_offset;

    if (session->scroll_offset <
        maximum)
    {
        size_t remaining =
            maximum -
            session->scroll_offset;

        size_t movement =
            lines < remaining
                ? lines
                : remaining;

        session->scroll_offset +=
            movement;
    }

    bool changed =
        session->scroll_offset !=
        old_offset;

    spin_unlock_irqrestore(
        &session->lock,
        flags);

    return changed;
}

static bool gui_terminal_scroll_view_down(
    gui_terminal_session_t *session,
    size_t lines)
{
    if (session == NULL ||
        lines == 0u)
    {
        return false;
    }

    uint32_t flags =
        spin_lock_irqsave(
            &session->lock);

    size_t old_offset =
        session->scroll_offset;

    if (lines >=
        session->scroll_offset)
    {
        session->scroll_offset =
            0u;
    }
    else
    {
        session->scroll_offset -=
            lines;
    }

    bool changed =
        session->scroll_offset !=
        old_offset;

    spin_unlock_irqrestore(
        &session->lock,
        flags);

    return changed;
}

/*
 * ------------------------------------------------------------
 * Terminal character parser
 * ------------------------------------------------------------
 */

static void gui_terminal_putchar_locked(
    gui_terminal_session_t *session,
    char character)
{
    if (session == NULL ||
        session->history_count == 0u)
    {
        return;
    }

    switch (character)
    {
    case '\a':
        /*
         * Bell intentionally remains silent.
         */
        return;

    case '\b':
    {
        if (session->column == 0u)
            return;

        --session->column;

        size_t line =
            gui_terminal_active_line(
                session);

        session->history[line]
                        [session->column]
                            .character =
            ' ';

        return;
    }

    case '\t':
        do
        {
            gui_terminal_putchar_locked(
                session,
                ' ');
        } while ((session->column %
                  4u) != 0u);

        return;

    case '\n':
        gui_terminal_newline_locked(
            session);

        return;

    case '\v':
    {
        /*
         * Vertical tab moves to a fresh line but preserves
         * horizontal position.
         */
        size_t preserved_column =
            session->column;

        gui_terminal_append_line_locked(
            session);

        session->column =
            preserved_column;

        return;
    }

    case '\f':
        /*
         * Meaty OS clear protocol.
         *
         * clear.nex emits exactly this form-feed byte.
         *
         * Clear scrollback as well as visible output.
         */
        gui_terminal_reset_history_locked(
            session);

        return;

    case '\r':
        session->column =
            0u;

        return;

    default:
        break;
    }

    /*
     * Ignore unsupported control bytes instead of turning them
     * into visible '?' characters.
     */
    if ((unsigned char)character <
        32u)
    {
        return;
    }

    if ((unsigned char)character >
        126u)
    {
        character =
            '?';
    }

    size_t line =
        gui_terminal_active_line(
            session);

    session->history[line]
                    [session->column]
                        .character =
        character;

    ++session->column;

    if (session->column >=
        GUI_TERMINAL_COLUMNS)
    {
        gui_terminal_newline_locked(
            session);
    }
}

/*
 * ------------------------------------------------------------
 * Rendering
 * ------------------------------------------------------------
 */

static bool gui_terminal_render(
    gui_terminal_session_t *session)
{
    if (session == NULL ||
        !session->used)
    {
        return false;
    }

    gui_surface_t *surface =
        gui_window_surface(
            &session->window);

    if (surface == NULL ||
        surface->pixels == NULL)
    {
        return false;
    }

    gui_font_t *font =
        gui_font_default();

    if (font == NULL)
        return false;

    /*
     * Complete terminal window remains opaque.
     */
    gui_surface_clear(
        surface,
        GUI_RGB(
            248u,
            250u,
            253u));

    gui_rect_t title_bar =
        {
            .x = 0,
            .y = 0,

            .width =
                surface->width,

            .height =
                GUI_TERMINAL_TITLE_HEIGHT};

    gui_surface_fill_rect(
        surface,
        title_bar,
        GUI_RGB(
            232u,
            236u,
            242u));

    gui_rect_t title_separator =
        {
            .x = 0,

            .y =
                GUI_TERMINAL_TITLE_HEIGHT -
                1,

            .width =
                surface->width,

            .height =
                1u};

    gui_surface_fill_rect(
        surface,
        title_separator,
        GUI_RGB(
            202u,
            208u,
            218u));

    if (!gui_font_draw_text(
            surface,
            font,
            14,
            8,
            15u,
            "Terminal",
            GUI_RGB(
                25u,
                30u,
                38u)))
    {
        return false;
    }

    /*
     * Snapshot the viewport while holding the terminal state lock.
     *
     * Glyph rendering is intentionally performed after releasing
     * the spinlock.
     */
    char visible[GUI_TERMINAL_ROWS]
                [GUI_TERMINAL_COLUMNS + 1u];

    for (size_t row = 0u;
         row <
         GUI_TERMINAL_ROWS;
         ++row)
    {
        for (size_t column = 0u;
             column <
             GUI_TERMINAL_COLUMNS;
             ++column)
        {
            visible[row][column] =
                ' ';
        }

        visible[row][GUI_TERMINAL_COLUMNS] =
            '\0';
    }

    uint32_t flags =
        spin_lock_irqsave(
            &session->lock);

    gui_terminal_clamp_scroll_offset(
        session);

    size_t visible_count =
        session->history_count <
                GUI_TERMINAL_ROWS
            ? session->history_count
            : GUI_TERMINAL_ROWS;

    size_t first_logical =
        0u;

    if (session->history_count >
        GUI_TERMINAL_ROWS)
    {
        first_logical =
            session->history_count -
            GUI_TERMINAL_ROWS -
            session->scroll_offset;
    }

    for (size_t row = 0u;
         row < visible_count;
         ++row)
    {
        size_t logical =
            first_logical +
            row;

        size_t physical =
            gui_terminal_logical_to_physical(
                session,
                logical);

        for (size_t column = 0u;
             column <
             GUI_TERMINAL_COLUMNS;
             ++column)
        {
            visible[row][column] =
                session->history[physical]
                                [column]
                                    .character;
        }
    }

    spin_unlock_irqrestore(
        &session->lock,
        flags);

    /*
     * Render the captured viewport.
     */
    for (size_t row = 0u;
         row < visible_count;
         ++row)
    {
        size_t length =
            GUI_TERMINAL_COLUMNS;

        while (length != 0u &&
               visible[row][length - 1u] ==
                   ' ')
        {
            --length;
        }

        visible[row][length] =
            '\0';

        if (length == 0u)
            continue;

        int32_t y =
            GUI_TERMINAL_TITLE_HEIGHT +
            GUI_TERMINAL_PADDING_Y +
            (int32_t)row *
                GUI_TERMINAL_LINE_HEIGHT;

        if (!gui_font_draw_text(
                surface,
                font,
                GUI_TERMINAL_PADDING_X,
                y,
                GUI_TERMINAL_FONT_HEIGHT,
                visible[row],
                GUI_RGB(
                    26u,
                    31u,
                    39u)))
        {
            return false;
        }
    }

    return true;
}

static void gui_terminal_present(
    gui_terminal_session_t *session)
{
    if (session == NULL ||
        !session->used)
    {
        return;
    }

    if (!gui_terminal_render(
            session))
    {
        return;
    }

    gui_window_composite(
        &session->window);

    gui_compositor_present();
}

void gui_terminal_session_write(
    gui_terminal_session_t *session,
    const char *buffer,
    size_t length)
{
    if (session == NULL ||
        buffer == NULL ||
        length == 0u)
    {
        return;
    }

    uint32_t flags =
        spin_lock_irqsave(
            &session->lock);

    for (size_t index = 0u;
         index < length;
         ++index)
    {
        gui_terminal_putchar_locked(
            session,
            buffer[index]);
    }

    spin_unlock_irqrestore(
        &session->lock,
        flags);

    gui_terminal_present(
        session);
}

/*
 * ------------------------------------------------------------
 * Process association
 * ------------------------------------------------------------
 */

static gui_terminal_session_t *
gui_terminal_session_find_root(
    process_id_t pid)
{
    if (pid ==
        PROCESS_ID_INVALID)
    {
        return NULL;
    }

    for (size_t index = 0u;
         index <
         GUI_TERMINAL_MAX_SESSIONS;
         ++index)
    {
        gui_terminal_session_t *session =
            &terminal_sessions[index];

        if (session->used &&
            session->root_pid ==
                pid)
        {
            return session;
        }
    }

    return NULL;
}

gui_terminal_session_t *
gui_terminal_session_for_process(
    process_t *process)
{
    if (process == NULL)
        return NULL;

    process_id_t pid =
        process_id(
            process);

    process_id_t parent =
        process_parent_id(
            process);

    /*
     * Check this process first.
     */
    gui_terminal_session_t *session =
        gui_terminal_session_find_root(
            pid);

    if (session != NULL)
        return session;

    /*
     * Commands spawned by sh.nex walk upward until the root shell
     * process bound to this Terminal is found.
     */
    for (size_t depth = 0u;
         depth < 32u &&
         parent !=
             PROCESS_ID_INVALID;
         ++depth)
    {
        session =
            gui_terminal_session_find_root(
                parent);

        if (session != NULL)
            return session;

        process_t *ancestor =
            process_acquire_by_id(
                parent);

        if (ancestor == NULL)
            return NULL;

        parent =
            process_parent_id(
                ancestor);

        process_release(
            ancestor);
    }

    return NULL;
}

/*
 * ------------------------------------------------------------
 * Keyboard routing
 * ------------------------------------------------------------
 */

bool gui_terminal_session_handle_keyboard(
    const keyboard_event_t *event)
{
    if (event == NULL ||
        !event->pressed)
    {
        return false;
    }

    gui_window_t *active =
        gui_window_active();

    if (active == NULL)
        return false;

    gui_terminal_session_t *session =
        NULL;

    for (size_t index = 0u;
         index <
         GUI_TERMINAL_MAX_SESSIONS;
         ++index)
    {
        if (terminal_sessions[index].used &&
            &terminal_sessions[index].window ==
                active)
        {
            session =
                &terminal_sessions[index];

            break;
        }
    }

    if (session == NULL)
        return false;

    /*
     * PageUp/PageDown belong to the Terminal viewport rather than
     * the shell.
     *
     * This gives us a way to validate scrollback before PS/2 wheel
     * support is added.
     */
    if (event->key ==
        KEY_PAGE_UP)
    {
        if (gui_terminal_scroll_view_up(
                session,
                GUI_TERMINAL_PAGE_SCROLL))
        {
            gui_terminal_present(
                session);
        }

        return true;
    }

    if (event->key ==
        KEY_PAGE_DOWN)
    {
        if (gui_terminal_scroll_view_down(
                session,
                GUI_TERMINAL_PAGE_SCROLL))
        {
            gui_terminal_present(
                session);
        }

        return true;
    }

    /*
     * Any ordinary keyboard input while viewing old history snaps
     * back to the live bottom.
     *
     * This matches normal interactive terminal behavior and avoids
     * typing commands while unable to see the current prompt.
     */
    if (event->character != '\0')
    {
        bool viewport_changed =
            false;

        uint32_t flags =
            spin_lock_irqsave(
                &session->lock);

        if (session->scroll_offset !=
            0u)
        {
            session->scroll_offset =
                0u;

            viewport_changed =
                true;
        }

        spin_unlock_irqrestore(
            &session->lock,
            flags);

        if (viewport_changed)
        {
            gui_terminal_present(
                session);
        }

        return gui_terminal_input_push(
            session,
            event->character);
    }

    /*
     * Existing sh.nex line editing understands these ANSI-style
     * sequences.
     */
    switch (event->key)
    {
    case KEY_ARROW_UP:
        gui_terminal_input_sequence(
            session,
            "\x1b[A");
        return true;

    case KEY_ARROW_DOWN:
        gui_terminal_input_sequence(
            session,
            "\x1b[B");
        return true;

    case KEY_ARROW_RIGHT:
        gui_terminal_input_sequence(
            session,
            "\x1b[C");
        return true;

    case KEY_ARROW_LEFT:
        gui_terminal_input_sequence(
            session,
            "\x1b[D");
        return true;

    case KEY_HOME:
        gui_terminal_input_sequence(
            session,
            "\x1b[H");
        return true;

    case KEY_END:
        gui_terminal_input_sequence(
            session,
            "\x1b[F");
        return true;

    case KEY_DELETE:
        gui_terminal_input_sequence(
            session,
            "\x1b[3~");
        return true;

    default:
        break;
    }

    return false;
}

/*
 * ------------------------------------------------------------
 * Session creation / launch
 * ------------------------------------------------------------
 */

static gui_terminal_session_t *
gui_terminal_allocate(void)
{
    for (size_t index = 0u;
         index <
         GUI_TERMINAL_MAX_SESSIONS;
         ++index)
    {
        gui_terminal_session_t *session =
            &terminal_sessions[index];

        if (session->used)
            continue;

        memset(
            session,
            0,
            sizeof(*session));

        spinlock_initialize(
            &session->lock);

        /*
         * Start with exactly one empty logical line.
         */
        gui_terminal_reset_history_locked(
            session);

        int32_t x =
            GUI_TERMINAL_START_X +
            (int32_t)index *
                GUI_TERMINAL_CASCADE;

        int32_t y =
            GUI_TERMINAL_START_Y +
            (int32_t)index *
                GUI_TERMINAL_CASCADE;

        if (!gui_window_create(
                &session->window,
                x,
                y,
                GUI_TERMINAL_WINDOW_WIDTH,
                GUI_TERMINAL_WINDOW_HEIGHT,
                GUI_Z_NORMAL))
        {
            memset(
                session,
                0,
                sizeof(*session));

            return NULL;
        }

        session->used =
            true;

        return session;
    }

    return NULL;
}

static void gui_terminal_prepare_spawn(
    process_id_t pid,
    void *context)
{
    gui_terminal_session_t *session =
        (gui_terminal_session_t *)
            context;

    if (session == NULL)
        return;

    /*
     * This callback runs before task_publish().
     *
     * Therefore when the process executes its first userspace
     * instruction, gui_terminal_session_for_process() can already
     * resolve it.
     */
    session->root_pid =
        pid;
}

bool gui_terminal_session_launch(
    const char *path)
{
    if (path == NULL ||
        path[0] != '/')
    {
        return false;
    }

    gui_terminal_session_t *session =
        gui_terminal_allocate();

    if (session == NULL)
        return false;

    if (!gui_terminal_render(
            session))
    {
        gui_window_destroy(
            &session->window);

        memset(
            session,
            0,
            sizeof(*session));

        return false;
    }

    /*
     * Put the Terminal into the normal-window focus stack before
     * its process begins producing output.
     */
    (void)gui_window_focus(
        &session->window);

    gui_desktop_render();

    const char *argv[1];

    argv[0] =
        path;

    /*
     * Establish session ownership BEFORE the task becomes runnable.
     *
     * This removes the race where sh.nex could print its initial
     * banner/prompt before session->root_pid was known.
     */
    process_id_t pid =
        process_spawn_user_prepared(
            path,
            1u,
            argv,
            gui_terminal_prepare_spawn,
            session);

    if (pid ==
        PROCESS_ID_INVALID)
    {
        session->root_pid =
            PROCESS_ID_INVALID;

        gui_window_destroy(
            &session->window);

        memset(
            session,
            0,
            sizeof(*session));

        gui_desktop_render();

        return false;
    }

    /*
     * gui_terminal_prepare_spawn() already established root_pid
     * before publication.
     *
     * Verify the invariant rather than assigning it here after the
     * process has potentially begun execution.
     */
    if (session->root_pid !=
        pid)
    {
        return false;
    }

    return true;
}

