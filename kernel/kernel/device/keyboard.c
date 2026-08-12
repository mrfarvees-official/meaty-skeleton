#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/keyboard.h>
#include <kernel/semaphore.h>

#include "../arch/i386/interrupts.h"
#include "../arch/i386/io.h"
#include "../arch/i386/pic.h"


#define KEYBOARD_IRQ       1u
#define KEYBOARD_DATA_PORT 0x60u


#define KEYBOARD_RAW_BUFFER_SIZE       128u
#define KEYBOARD_EVENT_BUFFER_SIZE     128u
#define KEYBOARD_CHARACTER_BUFFER_SIZE 128u


_Static_assert(
    (
        KEYBOARD_RAW_BUFFER_SIZE &
        (KEYBOARD_RAW_BUFFER_SIZE - 1u)
    ) == 0,
    "keyboard raw buffer size must be a power of two"
);


_Static_assert(
    (
        KEYBOARD_EVENT_BUFFER_SIZE &
        (KEYBOARD_EVENT_BUFFER_SIZE - 1u)
    ) == 0,
    "keyboard event buffer size must be a power of two"
);


_Static_assert(
    (
        KEYBOARD_CHARACTER_BUFFER_SIZE &
        (KEYBOARD_CHARACTER_BUFFER_SIZE - 1u)
    ) == 0,
    "keyboard character buffer size must be a power of two"
);


/*
 * ==========================================================================
 * RAW SCANCODE BUFFER
 * ==========================================================================
 */

static volatile uint8_t raw_buffer[
    KEYBOARD_RAW_BUFFER_SIZE
];

static volatile size_t raw_read_index = 0;
static volatile size_t raw_write_index = 0;

static volatile uint32_t raw_drop_count = 0;


/*
 * ==========================================================================
 * EVENT BUFFER
 * ==========================================================================
 */

static keyboard_event_t event_buffer[
    KEYBOARD_EVENT_BUFFER_SIZE
];

static volatile size_t event_read_index = 0;
static volatile size_t event_write_index = 0;

static volatile uint32_t event_drop_count = 0;


/*
 * One permit exists for every event currently stored in event_buffer.
 *
 * IRQ:
 *
 *     push event
 *     signal semaphore
 *
 * Reader:
 *
 *     wait semaphore
 *     pop event
 */
static semaphore_t event_available;


/*
 * ==========================================================================
 * CHARACTER BUFFER
 * ==========================================================================
 */

static char character_buffer[
    KEYBOARD_CHARACTER_BUFFER_SIZE
];

static volatile size_t character_read_index = 0;
static volatile size_t character_write_index = 0;

static volatile uint32_t character_drop_count = 0;


/*
 * One permit per character currently stored.
 */
static semaphore_t character_available;


/*
 * ==========================================================================
 * DECODER STATE
 * ==========================================================================
 */

static bool decoder_e0_pending = false;

static uint8_t decoder_e1_bytes_remaining = 0;


/*
 * Physical key-down state.
 */
static bool key_down[KEY_COUNT];


/*
 * Current modifier state.
 */
static keyboard_modifiers_t modifiers;


static bool keyboard_initialized = false;


/*
 * ==========================================================================
 * INDEX HELPERS
 * ==========================================================================
 */

static size_t raw_next_index(
    size_t index)
{
    return
        (index + 1u) &
        (KEYBOARD_RAW_BUFFER_SIZE - 1u);
}


static size_t event_next_index(
    size_t index)
{
    return
        (index + 1u) &
        (KEYBOARD_EVENT_BUFFER_SIZE - 1u);
}


static size_t character_next_index(
    size_t index)
{
    return
        (index + 1u) &
        (KEYBOARD_CHARACTER_BUFFER_SIZE - 1u);
}


/*
 * ==========================================================================
 * NORMAL SET 1 DECODER
 * ==========================================================================
 */

static keyboard_key_t decode_normal_key(
    uint8_t code)
{
    switch (code)
    {
        case 0x01: return KEY_ESCAPE;

        case 0x02: return KEY_1;
        case 0x03: return KEY_2;
        case 0x04: return KEY_3;
        case 0x05: return KEY_4;
        case 0x06: return KEY_5;
        case 0x07: return KEY_6;
        case 0x08: return KEY_7;
        case 0x09: return KEY_8;
        case 0x0A: return KEY_9;
        case 0x0B: return KEY_0;

        case 0x0C: return KEY_MINUS;
        case 0x0D: return KEY_EQUALS;

        case 0x0E: return KEY_BACKSPACE;
        case 0x0F: return KEY_TAB;

        case 0x10: return KEY_Q;
        case 0x11: return KEY_W;
        case 0x12: return KEY_E;
        case 0x13: return KEY_R;
        case 0x14: return KEY_T;
        case 0x15: return KEY_Y;
        case 0x16: return KEY_U;
        case 0x17: return KEY_I;
        case 0x18: return KEY_O;
        case 0x19: return KEY_P;

        case 0x1A: return KEY_LEFT_BRACKET;
        case 0x1B: return KEY_RIGHT_BRACKET;

        case 0x1C: return KEY_ENTER;
        case 0x1D: return KEY_LEFT_CTRL;

        case 0x1E: return KEY_A;
        case 0x1F: return KEY_S;
        case 0x20: return KEY_D;
        case 0x21: return KEY_F;
        case 0x22: return KEY_G;
        case 0x23: return KEY_H;
        case 0x24: return KEY_J;
        case 0x25: return KEY_K;
        case 0x26: return KEY_L;

        case 0x27: return KEY_SEMICOLON;
        case 0x28: return KEY_APOSTROPHE;
        case 0x29: return KEY_GRAVE;

        case 0x2A: return KEY_LEFT_SHIFT;
        case 0x2B: return KEY_BACKSLASH;

        case 0x2C: return KEY_Z;
        case 0x2D: return KEY_X;
        case 0x2E: return KEY_C;
        case 0x2F: return KEY_V;
        case 0x30: return KEY_B;
        case 0x31: return KEY_N;
        case 0x32: return KEY_M;

        case 0x33: return KEY_COMMA;
        case 0x34: return KEY_PERIOD;
        case 0x35: return KEY_SLASH;

        case 0x36: return KEY_RIGHT_SHIFT;

        case 0x37: return KEY_KP_MULTIPLY;

        case 0x38: return KEY_LEFT_ALT;
        case 0x39: return KEY_SPACE;

        case 0x3A: return KEY_CAPS_LOCK;

        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;
        case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;
        case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;
        case 0x44: return KEY_F10;

        case 0x45: return KEY_NUM_LOCK;
        case 0x46: return KEY_SCROLL_LOCK;

        case 0x47: return KEY_KP_7;
        case 0x48: return KEY_KP_8;
        case 0x49: return KEY_KP_9;

        case 0x4A: return KEY_KP_MINUS;

        case 0x4B: return KEY_KP_4;
        case 0x4C: return KEY_KP_5;
        case 0x4D: return KEY_KP_6;

        case 0x4E: return KEY_KP_PLUS;

        case 0x4F: return KEY_KP_1;
        case 0x50: return KEY_KP_2;
        case 0x51: return KEY_KP_3;

        case 0x52: return KEY_KP_0;
        case 0x53: return KEY_KP_PERIOD;

        case 0x57: return KEY_F11;
        case 0x58: return KEY_F12;

        default:
            return KEY_UNKNOWN;
    }
}


/*
 * ==========================================================================
 * E0 EXTENDED SET 1 DECODER
 * ==========================================================================
 */

static keyboard_key_t decode_extended_key(
    uint8_t code)
{
    switch (code)
    {
        case 0x1C:
            return KEY_KP_ENTER;

        case 0x1D:
            return KEY_RIGHT_CTRL;

        case 0x35:
            return KEY_KP_SLASH;

        case 0x38:
            return KEY_RIGHT_ALT;

        case 0x47:
            return KEY_HOME;

        case 0x48:
            return KEY_ARROW_UP;

        case 0x49:
            return KEY_PAGE_UP;

        case 0x4B:
            return KEY_ARROW_LEFT;

        case 0x4D:
            return KEY_ARROW_RIGHT;

        case 0x4F:
            return KEY_END;

        case 0x50:
            return KEY_ARROW_DOWN;

        case 0x51:
            return KEY_PAGE_DOWN;

        case 0x52:
            return KEY_INSERT;

        case 0x53:
            return KEY_DELETE;

        default:
            return KEY_UNKNOWN;
    }
}


/*
 * ==========================================================================
 * RAW BUFFER
 * ==========================================================================
 */

static void keyboard_raw_push(
    uint8_t scancode)
{
    size_t write_index =
        raw_write_index;

    size_t next =
        raw_next_index(
            write_index
        );


    if (next ==
        raw_read_index)
    {
        ++raw_drop_count;
        return;
    }


    raw_buffer[write_index] =
        scancode;

    raw_write_index =
        next;
}


/*
 * ==========================================================================
 * EVENT BUFFER
 * ==========================================================================
 *
 * Called from IRQ context.
 */

static void keyboard_event_push(
    const keyboard_event_t *event)
{
    if (event == NULL)
        return;


    size_t write_index =
        event_write_index;

    size_t next =
        event_next_index(
            write_index
        );


    if (next ==
        event_read_index)
    {
        ++event_drop_count;

        /*
         * No semaphore signal because no event was inserted.
         */
        return;
    }


    event_buffer[write_index] =
        *event;


    /*
     * Publish queue contents before publishing availability.
     */
    event_write_index =
        next;


    /*
     * This is safe in the current design:
     *
     * semaphore_signal() does not block.
     *
     * It may wake one blocked task through scheduler_wake(), but the
     * actual context switch happens later at your interrupt dispatcher's
     * safe preemption point.
     */
    semaphore_signal(
        &event_available
    );
}


/*
 * ==========================================================================
 * CHARACTER BUFFER
 * ==========================================================================
 *
 * Called from IRQ context.
 */

static void keyboard_character_push(
    char character)
{
    if (character == '\0')
        return;


    size_t write_index =
        character_write_index;

    size_t next =
        character_next_index(
            write_index
        );


    if (next ==
        character_read_index)
    {
        ++character_drop_count;

        return;
    }


    character_buffer[write_index] =
        character;


    character_write_index =
        next;


    /*
     * Wake one character reader.
     */
    semaphore_signal(
        &character_available
    );
}


/*
 * ==========================================================================
 * QUEUE POP HELPERS
 * ==========================================================================
 *
 * Caller must already have interrupts disabled.
 */

static bool keyboard_event_pop_unlocked(
    keyboard_event_t *event)
{
    if (event == NULL)
        return false;


    if (event_read_index ==
        event_write_index)
    {
        return false;
    }


    *event =
        event_buffer[
            event_read_index
        ];


    event_read_index =
        event_next_index(
            event_read_index
        );


    return true;
}


static bool keyboard_character_pop_unlocked(
    char *character)
{
    if (character == NULL)
        return false;


    if (character_read_index ==
        character_write_index)
    {
        return false;
    }


    *character =
        character_buffer[
            character_read_index
        ];


    character_read_index =
        character_next_index(
            character_read_index
        );


    return true;
}


/*
 * ==========================================================================
 * MODIFIER HELPERS
 * ==========================================================================
 */

static bool shift_active(void)
{
    return
        modifiers.left_shift ||
        modifiers.right_shift;
}


static void keyboard_update_modifier_state(
    keyboard_key_t key,
    bool pressed,
    bool was_down)
{
    switch (key)
    {
        case KEY_LEFT_SHIFT:
            modifiers.left_shift =
                pressed;
            break;

        case KEY_RIGHT_SHIFT:
            modifiers.right_shift =
                pressed;
            break;

        case KEY_LEFT_CTRL:
            modifiers.left_ctrl =
                pressed;
            break;

        case KEY_RIGHT_CTRL:
            modifiers.right_ctrl =
                pressed;
            break;

        case KEY_LEFT_ALT:
            modifiers.left_alt =
                pressed;
            break;

        case KEY_RIGHT_ALT:
            modifiers.right_alt =
                pressed;
            break;

        case KEY_CAPS_LOCK:
            /*
             * Toggle only on a genuine UP -> DOWN transition.
             */
            if (pressed &&
                !was_down)
            {
                modifiers.caps_lock =
                    !modifiers.caps_lock;
            }

            break;

        default:
            break;
    }
}


/*
 * ==========================================================================
 * CHARACTER TRANSLATION
 * ==========================================================================
 */

static char keyboard_translate_letter(
    keyboard_key_t key,
    bool uppercase)
{
    char character;


    switch (key)
    {
        case KEY_A: character = 'a'; break;
        case KEY_B: character = 'b'; break;
        case KEY_C: character = 'c'; break;
        case KEY_D: character = 'd'; break;
        case KEY_E: character = 'e'; break;
        case KEY_F: character = 'f'; break;
        case KEY_G: character = 'g'; break;
        case KEY_H: character = 'h'; break;
        case KEY_I: character = 'i'; break;
        case KEY_J: character = 'j'; break;
        case KEY_K: character = 'k'; break;
        case KEY_L: character = 'l'; break;
        case KEY_M: character = 'm'; break;
        case KEY_N: character = 'n'; break;
        case KEY_O: character = 'o'; break;
        case KEY_P: character = 'p'; break;
        case KEY_Q: character = 'q'; break;
        case KEY_R: character = 'r'; break;
        case KEY_S: character = 's'; break;
        case KEY_T: character = 't'; break;
        case KEY_U: character = 'u'; break;
        case KEY_V: character = 'v'; break;
        case KEY_W: character = 'w'; break;
        case KEY_X: character = 'x'; break;
        case KEY_Y: character = 'y'; break;
        case KEY_Z: character = 'z'; break;

        default:
            return '\0';
    }


    if (uppercase)
    {
        character =
            (char)(
                character -
                'a' +
                'A'
            );
    }


    return character;
}


static char keyboard_translate_character(
    keyboard_key_t key)
{
    bool shift =
        shift_active();


    bool uppercase =
        shift !=
        modifiers.caps_lock;


    char letter =
        keyboard_translate_letter(
            key,
            uppercase
        );


    if (letter != '\0')
        return letter;


    switch (key)
    {
        case KEY_1:
            return shift ? '!' : '1';

        case KEY_2:
            return shift ? '@' : '2';

        case KEY_3:
            return shift ? '#' : '3';

        case KEY_4:
            return shift ? '$' : '4';

        case KEY_5:
            return shift ? '%' : '5';

        case KEY_6:
            return shift ? '^' : '6';

        case KEY_7:
            return shift ? '&' : '7';

        case KEY_8:
            return shift ? '*' : '8';

        case KEY_9:
            return shift ? '(' : '9';

        case KEY_0:
            return shift ? ')' : '0';


        case KEY_MINUS:
            return shift ? '_' : '-';

        case KEY_EQUALS:
            return shift ? '+' : '=';


        case KEY_LEFT_BRACKET:
            return shift ? '{' : '[';

        case KEY_RIGHT_BRACKET:
            return shift ? '}' : ']';


        case KEY_SEMICOLON:
            return shift ? ':' : ';';

        case KEY_APOSTROPHE:
            return shift ? '"' : '\'';

        case KEY_GRAVE:
            return shift ? '~' : '`';


        case KEY_BACKSLASH:
            return shift ? '|' : '\\';


        case KEY_COMMA:
            return shift ? '<' : ',';

        case KEY_PERIOD:
            return shift ? '>' : '.';

        case KEY_SLASH:
            return shift ? '?' : '/';


        case KEY_SPACE:
            return ' ';


        case KEY_ENTER:
        case KEY_KP_ENTER:
            return '\n';


        case KEY_TAB:
            return '\t';


        case KEY_BACKSPACE:
            return '\b';


        default:
            return '\0';
    }
}


/*
 * ==========================================================================
 * SCANCODE DECODER
 * ==========================================================================
 */

static void keyboard_decode_scancode(
    uint8_t scancode)
{
    /*
     * Ignore remainder of E1 Pause sequence.
     */
    if (decoder_e1_bytes_remaining != 0)
    {
        --decoder_e1_bytes_remaining;
        return;
    }


    if (scancode == 0xE1u)
    {
        decoder_e1_bytes_remaining = 5u;
        decoder_e0_pending = false;

        return;
    }


    /*
     * E0 prefix.
     */
    if (scancode == 0xE0u)
    {
        decoder_e0_pending = true;
        return;
    }


    bool pressed =
        (scancode & 0x80u) == 0;


    uint8_t base_code =
        scancode & 0x7Fu;


    bool extended =
        decoder_e0_pending;


    decoder_e0_pending = false;


    keyboard_key_t key;


    if (extended)
    {
        key =
            decode_extended_key(
                base_code
            );
    }
    else
    {
        key =
            decode_normal_key(
                base_code
            );
    }


    if (key == KEY_UNKNOWN ||
        key >= KEY_COUNT)
    {
        return;
    }


    bool was_down =
        key_down[key];


    key_down[key] =
        pressed;


    keyboard_update_modifier_state(
        key,
        pressed,
        was_down
    );


    keyboard_event_t event;


    event.key =
        key;

    event.pressed =
        pressed;

    event.extended =
        extended;

    event.raw_scancode =
        base_code;

    event.modifiers =
        modifiers;


    if (pressed)
    {
        event.character =
            keyboard_translate_character(
                key
            );
    }
    else
    {
        event.character =
            '\0';
    }


    /*
     * Publish every supported key event.
     */
    keyboard_event_push(
        &event
    );


    /*
     * Publish text characters independently.
     *
     * Releases, Shift, Ctrl, arrows, etc. never enter this queue.
     */
    if (event.character != '\0')
    {
        keyboard_character_push(
            event.character
        );
    }
}


/*
 * ==========================================================================
 * IRQ1 HANDLER
 * ==========================================================================
 */

static void keyboard_interrupt_handler(
    struct interrupt_frame *frame)
{
    (void)frame;


    uint8_t scancode =
        inb(KEYBOARD_DATA_PORT);


    keyboard_raw_push(
        scancode
    );


    keyboard_decode_scancode(
        scancode
    );


    /*
     * No printf.
     * No allocation.
     * No blocking.
     * No explicit scheduling.
     *
     * Generic interrupt dispatcher sends EOI.
     */
}


/*
 * ==========================================================================
 * INITIALIZATION
 * ==========================================================================
 */

bool keyboard_initialize(void)
{
    if (keyboard_initialized)
        return true;


    uint32_t flags =
        interrupt_save_disable();


    /*
     * Raw queue.
     */
    raw_read_index = 0;
    raw_write_index = 0;
    raw_drop_count = 0;


    /*
     * Event queue.
     */
    event_read_index = 0;
    event_write_index = 0;
    event_drop_count = 0;


    /*
     * Character queue.
     */
    character_read_index = 0;
    character_write_index = 0;
    character_drop_count = 0;


    /*
     * Blocking queue counters.
     */
    semaphore_initialize(
        &event_available,
        0
    );

    semaphore_initialize(
        &character_available,
        0
    );


    /*
     * Decoder.
     */
    decoder_e0_pending = false;
    decoder_e1_bytes_remaining = 0;


    /*
     * Physical key state.
     */
    for (size_t i = 0;
         i < KEY_COUNT;
         ++i)
    {
        key_down[i] = false;
    }


    /*
     * Modifier state.
     */
    modifiers.left_shift = false;
    modifiers.right_shift = false;

    modifiers.left_ctrl = false;
    modifiers.right_ctrl = false;

    modifiers.left_alt = false;
    modifiers.right_alt = false;

    modifiers.caps_lock = false;


    /*
     * Install IRQ1 handler before unmasking IRQ1.
     */
    uint8_t vector =
        pic_irq_to_vector(
            KEYBOARD_IRQ
        );


    if (interrupt_register_handler(
            vector,
            keyboard_interrupt_handler)
        != 0)
    {
        interrupt_restore(flags);

        return false;
    }


    pic_unmask(
        KEYBOARD_IRQ
    );


    keyboard_initialized =
        true;


    interrupt_restore(flags);


    return true;
}


/*
 * ==========================================================================
 * RAW API
 * ==========================================================================
 */

bool keyboard_read_scancode(
    uint8_t *scancode)
{
    if (scancode == NULL)
        return false;


    uint32_t flags =
        interrupt_save_disable();


    if (raw_read_index ==
        raw_write_index)
    {
        interrupt_restore(flags);
        return false;
    }


    *scancode =
        raw_buffer[
            raw_read_index
        ];


    raw_read_index =
        raw_next_index(
            raw_read_index
        );


    interrupt_restore(flags);


    return true;
}


size_t keyboard_pending_scancodes(void)
{
    uint32_t flags =
        interrupt_save_disable();


    size_t read_index =
        raw_read_index;

    size_t write_index =
        raw_write_index;


    size_t count;


    if (write_index >=
        read_index)
    {
        count =
            write_index -
            read_index;
    }
    else
    {
        count =
            KEYBOARD_RAW_BUFFER_SIZE -
            read_index +
            write_index;
    }


    interrupt_restore(flags);


    return count;
}


uint32_t keyboard_dropped_scancodes(void)
{
    uint32_t flags =
        interrupt_save_disable();


    uint32_t count =
        raw_drop_count;


    interrupt_restore(flags);


    return count;
}


/*
 * ==========================================================================
 * NON-BLOCKING EVENT API
 * ==========================================================================
 */

bool keyboard_read_event(
    keyboard_event_t *event)
{
    if (event == NULL)
        return false;


    /*
     * Important:
     *
     * The semaphore count must stay synchronized with the number of
     * events in the ring.
     *
     * Therefore even the NON-BLOCKING reader consumes one semaphore
     * permit first.
     */
    if (!semaphore_try_wait(
            &event_available))
    {
        return false;
    }


    uint32_t flags =
        interrupt_save_disable();


    bool result =
        keyboard_event_pop_unlocked(
            event
        );


    interrupt_restore(flags);


    return result;
}


/*
 * ==========================================================================
 * BLOCKING EVENT API
 * ==========================================================================
 */

bool keyboard_wait_event(
    keyboard_event_t *event)
{
    if (event == NULL)
        return false;


    /*
     * This is the blocking point.
     *
     * If no event permit exists:
     *
     *     semaphore_wait()
     *         ↓
     *     wait_queue_block()
     *         ↓
     *     TASK_BLOCKED
     *         ↓
     *     scheduler runs something else
     *
     * IRQ1 later calls semaphore_signal() and wakes a waiter.
     */
    if (!semaphore_wait(
            &event_available))
    {
        return false;
    }


    /*
     * Once a semaphore permit has been acquired, exactly one event
     * should exist for us.
     */
    uint32_t flags =
        interrupt_save_disable();


    bool result =
        keyboard_event_pop_unlocked(
            event
        );


    interrupt_restore(flags);


    return result;
}


size_t keyboard_pending_events(void)
{
    /*
     * Semaphore count directly represents queued events.
     */
    return semaphore_get_count(
        &event_available
    );
}


uint32_t keyboard_dropped_events(void)
{
    uint32_t flags =
        interrupt_save_disable();


    uint32_t count =
        event_drop_count;


    interrupt_restore(flags);


    return count;
}


/*
 * ==========================================================================
 * NON-BLOCKING CHARACTER API
 * ==========================================================================
 */

bool keyboard_read_character(
    char *character)
{
    if (character == NULL)
        return false;


    if (!semaphore_try_wait(
            &character_available))
    {
        return false;
    }


    uint32_t flags =
        interrupt_save_disable();


    bool result =
        keyboard_character_pop_unlocked(
            character
        );


    interrupt_restore(flags);


    return result;
}


/*
 * ==========================================================================
 * BLOCKING CHARACTER API
 * ==========================================================================
 */

bool keyboard_wait_character(
    char *character)
{
    if (character == NULL)
        return false;


    /*
     * No polling.
     *
     * The calling task sleeps here until IRQ1 produces an actual text
     * character.
     */
    if (!semaphore_wait(
            &character_available))
    {
        return false;
    }


    uint32_t flags =
        interrupt_save_disable();


    bool result =
        keyboard_character_pop_unlocked(
            character
        );


    interrupt_restore(flags);


    return result;
}


size_t keyboard_pending_characters(void)
{
    return semaphore_get_count(
        &character_available
    );
}


uint32_t keyboard_dropped_characters(void)
{
    uint32_t flags =
        interrupt_save_disable();


    uint32_t count =
        character_drop_count;


    interrupt_restore(flags);


    return count;
}


/*
 * ==========================================================================
 * MODIFIER STATE
 * ==========================================================================
 */

keyboard_modifiers_t keyboard_get_modifiers(void)
{
    uint32_t flags =
        interrupt_save_disable();


    keyboard_modifiers_t result =
        modifiers;


    interrupt_restore(flags);


    return result;
}


/*
 * ==========================================================================
 * KEY NAMES
 * ==========================================================================
 */

const char *keyboard_key_name(
    keyboard_key_t key)
{
    switch (key)
    {
        case KEY_ESCAPE: return "ESCAPE";

        case KEY_1: return "1";
        case KEY_2: return "2";
        case KEY_3: return "3";
        case KEY_4: return "4";
        case KEY_5: return "5";
        case KEY_6: return "6";
        case KEY_7: return "7";
        case KEY_8: return "8";
        case KEY_9: return "9";
        case KEY_0: return "0";

        case KEY_MINUS: return "MINUS";
        case KEY_EQUALS: return "EQUALS";

        case KEY_BACKSPACE: return "BACKSPACE";
        case KEY_TAB: return "TAB";

        case KEY_Q: return "Q";
        case KEY_W: return "W";
        case KEY_E: return "E";
        case KEY_R: return "R";
        case KEY_T: return "T";
        case KEY_Y: return "Y";
        case KEY_U: return "U";
        case KEY_I: return "I";
        case KEY_O: return "O";
        case KEY_P: return "P";

        case KEY_LEFT_BRACKET: return "LEFT_BRACKET";
        case KEY_RIGHT_BRACKET: return "RIGHT_BRACKET";

        case KEY_ENTER: return "ENTER";
        case KEY_LEFT_CTRL: return "LEFT_CTRL";

        case KEY_A: return "A";
        case KEY_S: return "S";
        case KEY_D: return "D";
        case KEY_F: return "F";
        case KEY_G: return "G";
        case KEY_H: return "H";
        case KEY_J: return "J";
        case KEY_K: return "K";
        case KEY_L: return "L";

        case KEY_SEMICOLON: return "SEMICOLON";
        case KEY_APOSTROPHE: return "APOSTROPHE";
        case KEY_GRAVE: return "GRAVE";

        case KEY_LEFT_SHIFT: return "LEFT_SHIFT";
        case KEY_BACKSLASH: return "BACKSLASH";

        case KEY_Z: return "Z";
        case KEY_X: return "X";
        case KEY_C: return "C";
        case KEY_V: return "V";
        case KEY_B: return "B";
        case KEY_N: return "N";
        case KEY_M: return "M";

        case KEY_COMMA: return "COMMA";
        case KEY_PERIOD: return "PERIOD";
        case KEY_SLASH: return "SLASH";

        case KEY_RIGHT_SHIFT: return "RIGHT_SHIFT";

        case KEY_KP_MULTIPLY: return "KP_MULTIPLY";

        case KEY_LEFT_ALT: return "LEFT_ALT";
        case KEY_SPACE: return "SPACE";

        case KEY_CAPS_LOCK: return "CAPS_LOCK";

        case KEY_F1: return "F1";
        case KEY_F2: return "F2";
        case KEY_F3: return "F3";
        case KEY_F4: return "F4";
        case KEY_F5: return "F5";
        case KEY_F6: return "F6";
        case KEY_F7: return "F7";
        case KEY_F8: return "F8";
        case KEY_F9: return "F9";
        case KEY_F10: return "F10";

        case KEY_NUM_LOCK: return "NUM_LOCK";
        case KEY_SCROLL_LOCK: return "SCROLL_LOCK";

        case KEY_KP_7: return "KP_7";
        case KEY_KP_8: return "KP_8";
        case KEY_KP_9: return "KP_9";

        case KEY_KP_MINUS: return "KP_MINUS";

        case KEY_KP_4: return "KP_4";
        case KEY_KP_5: return "KP_5";
        case KEY_KP_6: return "KP_6";

        case KEY_KP_PLUS: return "KP_PLUS";

        case KEY_KP_1: return "KP_1";
        case KEY_KP_2: return "KP_2";
        case KEY_KP_3: return "KP_3";

        case KEY_KP_0: return "KP_0";
        case KEY_KP_PERIOD: return "KP_PERIOD";

        case KEY_F11: return "F11";
        case KEY_F12: return "F12";

        case KEY_KP_ENTER: return "KP_ENTER";
        case KEY_RIGHT_CTRL: return "RIGHT_CTRL";
        case KEY_KP_SLASH: return "KP_SLASH";
        case KEY_RIGHT_ALT: return "RIGHT_ALT";

        case KEY_HOME: return "HOME";
        case KEY_ARROW_UP: return "ARROW_UP";
        case KEY_PAGE_UP: return "PAGE_UP";

        case KEY_ARROW_LEFT: return "ARROW_LEFT";
        case KEY_ARROW_RIGHT: return "ARROW_RIGHT";

        case KEY_END: return "END";
        case KEY_ARROW_DOWN: return "ARROW_DOWN";
        case KEY_PAGE_DOWN: return "PAGE_DOWN";

        case KEY_INSERT: return "INSERT";
        case KEY_DELETE: return "DELETE";

        case KEY_UNKNOWN:
        case KEY_COUNT:
        default:
            return "UNKNOWN";
    }
}