#ifndef KERNEL_MOUSE_CURSOR_H
#define KERNEL_MOUSE_CURSOR_H

#include <stdbool.h>


bool mouse_cursor_initialize(void);


/*
 * Consume queued physical mouse events.
 *
 * MOVE events update the absolute cursor.
 *
 * Button events are forwarded to the GUI input dispatcher with the
 * exact absolute cursor position at which the transition occurred.
 */
void mouse_cursor_poll(void);


void mouse_cursor_begin_framebuffer_update(void);

void mouse_cursor_end_framebuffer_update(void);


#endif