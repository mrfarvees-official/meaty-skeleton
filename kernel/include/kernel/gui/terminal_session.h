#ifndef KERNEL_GUI_TERMINAL_SESSION_H
#define KERNEL_GUI_TERMINAL_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/keyboard.h>
#include <kernel/process.h>


typedef struct gui_terminal_session
    gui_terminal_session_t;


/*
 * Create one GUI terminal window and launch the supplied
 * userspace program inside it.
 */
bool gui_terminal_session_launch(
    const char *path);


/*
 * Resolve the terminal session that owns a process.
 *
 * Child processes inherit the session logically through
 * their parent process chain.
 */
gui_terminal_session_t *
gui_terminal_session_for_process(
    process_t *process);


/*
 * Standard input/output endpoint.
 *
 * read() is non-blocking so the current userspace shell's
 * yield-based input loop continues to work unchanged.
 */
size_t gui_terminal_session_read(
    gui_terminal_session_t *session,
    char *buffer,
    size_t capacity);

void gui_terminal_session_write(
    gui_terminal_session_t *session,
    const char *buffer,
    size_t length);


/*
 * Route a physical keyboard press to the currently focused
 * terminal window.
 */
bool gui_terminal_session_handle_keyboard(
    const keyboard_event_t *event);


#endif