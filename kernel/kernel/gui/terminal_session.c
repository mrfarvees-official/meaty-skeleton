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

typedef struct gui_terminal_cell
{
    char character;

} gui_terminal_cell_t;

struct gui_terminal_session
{
    bool used;

    process_id_t root_pid;

    gui_window_t window;

    gui_terminal_cell_t
        cells[GUI_TERMINAL_ROWS]
             [GUI_TERMINAL_COLUMNS];

    size_t row;
    size_t column;

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
 * Terminal grid
 * ------------------------------------------------------------
 */

static void gui_terminal_clear_row(
    gui_terminal_session_t *session,
    size_t row)
{
    if (session == NULL ||
        row >= GUI_TERMINAL_ROWS)
    {
        return;
    }

    for (size_t column = 0u;
         column <
         GUI_TERMINAL_COLUMNS;
         ++column)
    {
        session->cells[row][column].character =
            ' ';
    }
}

static void gui_terminal_scroll(
    gui_terminal_session_t *session)
{
    if (session == NULL)
        return;

    for (size_t row = 1u;
         row < GUI_TERMINAL_ROWS;
         ++row)
    {
        memcpy(
            session->cells[row - 1u],
            session->cells[row],
            sizeof(
                session->cells[row]));
    }

    gui_terminal_clear_row(
        session,
        GUI_TERMINAL_ROWS - 1u);

    session->row =
        GUI_TERMINAL_ROWS - 1u;
}

static void gui_terminal_newline(
    gui_terminal_session_t *session)
{
    session->column =
        0u;

    ++session->row;

    if (session->row >=
        GUI_TERMINAL_ROWS)
    {
        gui_terminal_scroll(
            session);
    }
}

static void gui_terminal_putchar_locked(
    gui_terminal_session_t *session,
    char character)
{
    if (session == NULL)
        return;

    switch (character)
    {
    case '\a':
        /*
         * Bell is intentionally silent for now.
         */
        return;

    case '\b':
        if (session->column != 0u)
        {
            --session->column;

            session->cells[session->row]
                          [session->column]
                              .character =
                ' ';
        }

        return;

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
        gui_terminal_newline(
            session);

        return;

    case '\v':
        /*
         * Vertical tab:
         * advance one row while preserving the current column.
         */
        ++session->row;

        if (session->row >=
            GUI_TERMINAL_ROWS)
        {
            gui_terminal_scroll(
                session);
        }

        return;

    case '\f':
        /*
         * Form feed is Meaty OS's existing terminal-clear
         * operation.
         *
         * clear.nex intentionally writes exactly this byte.
         */
        for (size_t row = 0u;
             row < GUI_TERMINAL_ROWS;
             ++row)
        {
            gui_terminal_clear_row(
                session,
                row);
        }

        session->row =
            0u;

        session->column =
            0u;

        return;

    case '\r':
        session->column =
            0u;

        return;

    default:
        break;
    }

    /*
     * Ignore unsupported control bytes instead of displaying
     * them as '?'.
     *
     * This prevents future terminal-control bytes from becoming
     * visible garbage while the parser is still minimal.
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

    session->cells[session->row]
                  [session->column]
                      .character =
        character;

    ++session->column;

    if (session->column >=
        GUI_TERMINAL_COLUMNS)
    {
        gui_terminal_newline(
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
     * The complete window is opaque.
     *
     * This matters because terminal output can composite this
     * window repeatedly without alpha accumulating over the
     * existing compositor backbuffer.
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

            .height = 1u};

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

    char line[GUI_TERMINAL_COLUMNS +
              1u];

    uint32_t flags =
        spin_lock_irqsave(
            &session->lock);

    for (size_t row = 0u;
         row < GUI_TERMINAL_ROWS;
         ++row)
    {
        for (size_t column = 0u;
             column <
             GUI_TERMINAL_COLUMNS;
             ++column)
        {
            line[column] =
                session->cells[row][column].character;
        }

        line[GUI_TERMINAL_COLUMNS] =
            '\0';

        /*
         * Trim trailing spaces only for rendering.
         */
        size_t length =
            GUI_TERMINAL_COLUMNS;

        while (length != 0u &&
               line[length - 1u] ==
                   ' ')
        {
            --length;
        }

        line[length] =
            '\0';

        if (length != 0u)
        {
            int32_t y =
                GUI_TERMINAL_TITLE_HEIGHT +
                GUI_TERMINAL_PADDING_Y +
                (int32_t)row *
                    GUI_TERMINAL_LINE_HEIGHT;

            /*
             * We intentionally release the lock before expensive
             * glyph rendering.
             */
            spin_unlock_irqrestore(
                &session->lock,
                flags);

            if (!gui_font_draw_text(
                    surface,
                    font,
                    GUI_TERMINAL_PADDING_X,
                    y,
                    GUI_TERMINAL_FONT_HEIGHT,
                    line,
                    GUI_RGB(
                        26u,
                        31u,
                        39u)))
            {
                return false;
            }

            flags =
                spin_lock_irqsave(
                    &session->lock);
        }
    }

    spin_unlock_irqrestore(
        &session->lock,
        flags);

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

    /*
     * The terminal is a GUI_Z_NORMAL window.
     *
     * Desktop reconstruction will therefore preserve it.
     */
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
     * Commands spawned by sh.nex keep walking upward until
     * the root shell process bound to this Terminal is found.
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
     * Normal translated text.
     */
    if (event->character != '\0')
    {
        return gui_terminal_input_push(
            session,
            event->character);
    }

    /*
     * Existing sh.nex line editing already understands these
     * ANSI-style sequences.
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

        for (size_t row = 0u;
             row <
             GUI_TERMINAL_ROWS;
             ++row)
        {
            gui_terminal_clear_row(
                session,
                row);
        }

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
     * Put the new Terminal in the normal-window focus stack
     * before its process starts producing output.
     */
    (void)gui_window_focus(
        &session->window);

    gui_desktop_render();

    const char *argv[1];

    argv[0] =
        path;

    process_id_t pid =
        process_spawn_user(
            path,
            1u,
            argv);

    if (pid ==
        PROCESS_ID_INVALID)
    {
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
     * Root process ownership.
     *
     * Children are resolved through the process parent chain,
     * so ls/cat/etc automatically use the same Terminal.
     */
    session->root_pid =
        pid;

    return true;
}