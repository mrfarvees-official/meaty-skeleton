#ifndef KERNEL_GUI_TASKBAR_H
#define KERNEL_GUI_TASKBAR_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/gui/input.h>
#include <kernel/mouse.h>


bool gui_taskbar_initialize(void);


/*
 * Composite the dock and any active GUI_Z_POPUP menu.
 */
void gui_taskbar_composite(void);


/*
 * Desktop-shell pointer dispatch.
 *
 * Coordinates are physical screen coordinates.
 *
 * Returns true if the taskbar or one of its popup menus consumed
 * the event.
 */
bool gui_taskbar_handle_pointer(
    gui_input_event_type_t type,
    int32_t x,
    int32_t y,
    mouse_button_t button);


#endif