#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/test.h>
#include <kernel/device/keyboard.h>
#include <kernel/task.h>


#define KEYBOARD_EVENT_TEST_COUNT 40u


void keyboard_event_test(void)
{
    printf(
        "\n[KEYBOARD EVENT TEST] starting\n"
    );


    printf(
        "[KEYBOARD EVENT TEST] "
        "press/release letters, Shift, Ctrl, "
        "Enter and arrow keys\n"
    );


    size_t received = 0;


    while (received <
           KEYBOARD_EVENT_TEST_COUNT)
    {
        keyboard_event_t event;


        if (!keyboard_read_event(
                &event))
        {
            /*
             * No event currently waiting.
             *
             * Don't waste CPU.
             */
            task_yield();

            continue;
        }


        printf(
            "[KEYBOARD EVENT] %-16s %s"
            " raw=0x%02x extended=%u\n",
            keyboard_key_name(
                event.key
            ),
            event.pressed
                ? "PRESSED "
                : "RELEASED",
            (unsigned)
                event.raw_scancode,
            event.extended
                ? 1u
                : 0u
        );


        ++received;
    }


    printf(
        "[KEYBOARD EVENT TEST] "
        "received: %u\n",
        (unsigned)received
    );


    printf(
        "[KEYBOARD EVENT TEST] "
        "raw dropped: %u\n",
        (unsigned)
            keyboard_dropped_scancodes()
    );


    printf(
        "[KEYBOARD EVENT TEST] "
        "event dropped: %u\n",
        (unsigned)
            keyboard_dropped_events()
    );


    if (keyboard_dropped_events() != 0)
    {
        printf(
            "[KEYBOARD EVENT TEST] "
            "FAIL: event queue overflow\n"
        );

        return;
    }


    printf(
        "[KEYBOARD EVENT TEST] PASS\n"
    );
}