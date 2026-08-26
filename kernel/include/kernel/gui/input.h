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

    /*
     * Already translated by the keyboard driver.
     *
     * '\0' means the key does not directly represent text.
     */
    char character;

} gui_input_event_t;


bool gui_input_initialize(void);


bool gui_input_filter_keyboard_event(
    const keyboard_event_t *event);


void gui_input_publish_mouse(
    gui_input_event_type_t type,
    int32_t x,
    int32_t y,
    mouse_button_t button,
    uint8_t buttons);


#endif