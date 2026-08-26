#ifndef KERNEL_GUI_INPUT_H
#define KERNEL_GUI_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/keyboard.h>
#include <kernel/mouse.h>


typedef enum gui_input_event_type
{
    GUI_INPUT_EVENT_MOUSE_MOVE = 0,

    GUI_INPUT_EVENT_MOUSE_BUTTON_DOWN,
    GUI_INPUT_EVENT_MOUSE_BUTTON_UP,

    GUI_INPUT_EVENT_KEY_DOWN,
    GUI_INPUT_EVENT_KEY_UP

} gui_input_event_type_t;


typedef struct gui_input_event
{
    gui_input_event_type_t type;

    int32_t mouse_x;
    int32_t mouse_y;

    mouse_button_t mouse_button;
    uint8_t mouse_buttons;

    keyboard_key_t key;
    keyboard_modifiers_t modifiers;

} gui_input_event_t;


/*
 * Initialize the GUI input dispatcher and its NORMAL kernel task.
 *
 * The task may be created before scheduling begins. It will not run
 * until the scheduler is enabled.
 */
bool gui_input_initialize(void);


/*
 * Keyboard event arbitration.
 *
 * Called from IRQ1 context.
 *
 * Every keyboard event needed by the GUI is copied into the GUI
 * event queue.
 *
 * Returns true when the event is a global GUI shortcut and therefore
 * must NOT also be published into the existing terminal/shell input
 * streams.
 *
 * Must never allocate, block, render, or perform filesystem work.
 */
bool gui_input_filter_keyboard_event(
    const keyboard_event_t *event);


/*
 * Publish an already-positioned mouse event.
 *
 * Called from normal mouse-cursor task context, not IRQ12.
 */
void gui_input_publish_mouse(
    gui_input_event_type_t type,
    int32_t x,
    int32_t y,
    mouse_button_t button,
    uint8_t buttons);


#endif