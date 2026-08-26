#ifndef KERNEL_GUI_TOPBAR_H
#define KERNEL_GUI_TOPBAR_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/gui/input.h>
#include <kernel/mouse.h>


bool gui_topbar_initialize(void);

bool gui_topbar_refresh(void);

void gui_topbar_composite(void);


/*
 * Desktop-shell pointer dispatch.
 *
 * Screen coordinates.
 *
 * Returns true when the topbar or one of its popup menus consumed
 * the event.
 */
bool gui_topbar_handle_pointer(
    gui_input_event_type_t type,
    int32_t x,
    int32_t y,
    mouse_button_t button);


#endif