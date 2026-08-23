#ifndef KERNEL_MOUSE_CURSOR_H
#define KERNEL_MOUSE_CURSOR_H

#include <stdbool.h>


/*
 * Initialize the first software framebuffer mouse cursor.
 *
 * Requires:
 *
 *     - framebuffer initialized
 *     - 32-bpp framebuffer mode active
 *     - PS/2 mouse initialized
 */
bool mouse_cursor_initialize(void);


/*
 * Consume queued physical mouse events.
 *
 * MOVE events update the absolute cursor position.
 * Button events are currently consumed and ignored.
 */
void mouse_cursor_poll(void);


/*
 * Coordinate framebuffer writers with the software cursor.
 *
 * begin:
 *     hides/restores the cursor before framebuffer contents change
 *
 * end:
 *     captures the new framebuffer contents and redraws the cursor
 *
 * Calls may be nested.
 */
void mouse_cursor_begin_framebuffer_update(void);

void mouse_cursor_end_framebuffer_update(void);


#endif