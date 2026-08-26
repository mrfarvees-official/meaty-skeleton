#ifndef KERNEL_GUI_TOPBAR_H
#define KERNEL_GUI_TOPBAR_H

#include <stdbool.h>


/*
 * Initialize the compositor-owned desktop system bar.
 */
bool gui_topbar_initialize(void);


/*
 * Rebuild dynamic system-bar contents.
 *
 * Currently updates:
 *
 *     date
 *     time
 */
bool gui_topbar_refresh(void);


/*
 * Composite the current system-bar surface.
 *
 * Does not present the framebuffer itself.
 */
void gui_topbar_composite(void);


#endif