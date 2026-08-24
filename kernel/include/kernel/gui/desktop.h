#ifndef KERNEL_GUI_DESKTOP_H
#define KERNEL_GUI_DESKTOP_H

#include <stdbool.h>

/*
 * Initialize the first compositor-owned desktop scene.
 *
 * The GUI compositor must already be initialized.
 *
 * Initialization performs the initial full-screen desktop render
 * and presents it to the framebuffer.
 */
bool gui_desktop_initialize(void);


/*
 * Render the complete desktop scene into the compositor backbuffer,
 * mark the complete display damaged and present it.
 *
 * For the first desktop milestone the scene is only a solid
 * background color.
 */
void gui_desktop_render(void);


#endif