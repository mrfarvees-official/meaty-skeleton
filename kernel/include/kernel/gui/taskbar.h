#ifndef KERNEL_GUI_TASKBAR_H
#define KERNEL_GUI_TASKBAR_H

#include <stdbool.h>


/*
 * Desktop-shell taskbar foundation.
 *
 * Initial milestone:
 *
 *     - owns one GUI_Z_TASKBAR window
 *     - draws only static shell chrome
 *     - no widgets
 *     - no buttons
 *     - no process/window enumeration
 *     - no input handling
 */
bool gui_taskbar_initialize(void);


/*
 * Composite the taskbar into the compositor backbuffer.
 *
 * The caller controls scene ordering.
 */
void gui_taskbar_composite(void);


#endif