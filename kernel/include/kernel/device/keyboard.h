#ifndef KERNEL_KEYBOARD_H
#define KERNEL_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/*
 * ==========================================================================
 * KEY CODES
 * ==========================================================================
 */

typedef enum keyboard_key
{
    KEY_UNKNOWN = 0,

    KEY_ESCAPE,

    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,
    KEY_0,

    KEY_MINUS,
    KEY_EQUALS,

    KEY_BACKSPACE,
    KEY_TAB,

    KEY_Q,
    KEY_W,
    KEY_E,
    KEY_R,
    KEY_T,
    KEY_Y,
    KEY_U,
    KEY_I,
    KEY_O,
    KEY_P,

    KEY_LEFT_BRACKET,
    KEY_RIGHT_BRACKET,

    KEY_ENTER,

    KEY_LEFT_CTRL,

    KEY_A,
    KEY_S,
    KEY_D,
    KEY_F,
    KEY_G,
    KEY_H,
    KEY_J,
    KEY_K,
    KEY_L,

    KEY_SEMICOLON,
    KEY_APOSTROPHE,
    KEY_GRAVE,

    KEY_LEFT_SHIFT,
    KEY_BACKSLASH,

    KEY_Z,
    KEY_X,
    KEY_C,
    KEY_V,
    KEY_B,
    KEY_N,
    KEY_M,

    KEY_COMMA,
    KEY_PERIOD,
    KEY_SLASH,

    KEY_RIGHT_SHIFT,

    KEY_KP_MULTIPLY,

    KEY_LEFT_ALT,
    KEY_SPACE,

    KEY_CAPS_LOCK,

    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,

    KEY_NUM_LOCK,
    KEY_SCROLL_LOCK,

    KEY_KP_7,
    KEY_KP_8,
    KEY_KP_9,

    KEY_KP_MINUS,

    KEY_KP_4,
    KEY_KP_5,
    KEY_KP_6,

    KEY_KP_PLUS,

    KEY_KP_1,
    KEY_KP_2,
    KEY_KP_3,

    KEY_KP_0,
    KEY_KP_PERIOD,

    KEY_F11,
    KEY_F12,

    /*
     * E0 extended keys.
     */
    KEY_KP_ENTER,
    KEY_RIGHT_CTRL,
    KEY_KP_SLASH,
    KEY_RIGHT_ALT,

    KEY_HOME,
    KEY_ARROW_UP,
    KEY_PAGE_UP,

    KEY_ARROW_LEFT,
    KEY_ARROW_RIGHT,

    KEY_END,
    KEY_ARROW_DOWN,
    KEY_PAGE_DOWN,

    KEY_INSERT,
    KEY_DELETE,

    KEY_COUNT

} keyboard_key_t;


/*
 * ==========================================================================
 * MODIFIER STATE
 * ==========================================================================
 */

typedef struct keyboard_modifiers
{
    bool left_shift;
    bool right_shift;

    bool left_ctrl;
    bool right_ctrl;

    bool left_alt;
    bool right_alt;

    bool caps_lock;

} keyboard_modifiers_t;


/*
 * ==========================================================================
 * KEYBOARD EVENT
 * ==========================================================================
 */

typedef struct keyboard_event
{
    keyboard_key_t key;

    bool pressed;
    bool extended;

    uint8_t raw_scancode;

    keyboard_modifiers_t modifiers;

    /*
     * '\0' means this event did not produce a character.
     *
     * Only key-press events may produce characters.
     */
    char character;

} keyboard_event_t;


/*
 * ==========================================================================
 * INITIALIZATION
 * ==========================================================================
 */

bool keyboard_initialize(void);


/*
 * ==========================================================================
 * RAW SCANCODE API
 * ==========================================================================
 *
 * Diagnostic API.
 */

bool keyboard_read_scancode(
    uint8_t *scancode
);

size_t keyboard_pending_scancodes(void);

uint32_t keyboard_dropped_scancodes(void);


/*
 * ==========================================================================
 * EVENT API
 * ==========================================================================
 */

/*
 * Non-blocking.
 *
 * Returns false immediately when no event is available.
 */
bool keyboard_read_event(
    keyboard_event_t *event
);


/*
 * Blocking.
 *
 * If no event exists, the current task enters TASK_BLOCKED and sleeps.
 *
 * IRQ1 wakes a waiting task when a new event is produced.
 *
 * Do not call from interrupt context.
 */
bool keyboard_wait_event(
    keyboard_event_t *event
);


size_t keyboard_pending_events(void);

uint32_t keyboard_dropped_events(void);


/*
 * ==========================================================================
 * CHARACTER API
 * ==========================================================================
 *
 * Character input has its OWN queue.
 *
 * Therefore consuming characters does not destroy key events from the
 * event queue.
 */


/*
 * Non-blocking character read.
 *
 * Returns false immediately if no character is available.
 */
bool keyboard_read_character(
    char *character
);


/*
 * Blocking character read.
 *
 * The current task sleeps until an actual text character is generated.
 *
 * Arrow keys, Shift, Ctrl, releases, etc. do not wake a character
 * reader because they do not generate characters.
 */
bool keyboard_wait_character(
    char *character
);


size_t keyboard_pending_characters(void);

uint32_t keyboard_dropped_characters(void);


/*
 * ==========================================================================
 * STATE / DEBUGGING
 * ==========================================================================
 */

keyboard_modifiers_t keyboard_get_modifiers(void);

const char *keyboard_key_name(
    keyboard_key_t key
);

/*
 * ==========================================================================
 * LINE INPUT
 * ==========================================================================
 */

/*
 * Read one complete line of text.
 *
 * This function BLOCKS until Enter is pressed.
 *
 * buffer:
 *     destination for the line
 *
 * capacity:
 *     total size of buffer, including the final '\0'
 *
 * length:
 *     receives the number of characters in the line,
 *     excluding the final '\0'
 *
 * echo:
 *     true  -> display typed characters and editing
 *     false -> collect input silently
 *
 * Returns:
 *
 *     true  -> a complete line was read
 *     false -> invalid arguments or keyboard input failure
 *
 * Examples:
 *
 *     char line[64];
 *     size_t length;
 *
 *     keyboard_read_line(
 *         line,
 *         sizeof(line),
 *         &length,
 *         true
 *     );
 *
 * A buffer with capacity 64 can contain at most:
 *
 *     63 characters + '\0'
 */
bool keyboard_read_line(
    char *buffer,
    size_t capacity,
    size_t *length,
    bool echo
);

#endif