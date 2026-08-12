#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/test.h>
#include <kernel/keyboard.h>
#include <kernel/task.h>


/*
 * Number of raw bytes to collect before ending the test.
 *
 * Pressing and releasing a normal key normally gives two bytes in
 * translated scan-code Set 1, so 32 bytes is enough for a simple
 * interactive test.
 */
#define KEYBOARD_RAW_TEST_BYTES 32u


void keyboard_raw_test(void)
{
    printf(
        "\n[KEYBOARD RAW TEST] starting\n"
    );


    printf(
        "[KEYBOARD RAW TEST] "
        "press and release some keys\n"
    );


    size_t received = 0;


    while (received <
           KEYBOARD_RAW_TEST_BYTES)
    {
        uint8_t scancode;


        if (!keyboard_read_scancode(
                &scancode))
        {
            /*
             * Nothing available.
             *
             * Do not busy-spin. Let other kernel tasks run.
             */
            task_yield();

            continue;
        }


        printf(
            "[KEYBOARD RAW] "
            "%02x\n",
            (unsigned)scancode
        );


        ++received;
    }


    printf(
        "[KEYBOARD RAW TEST] "
        "received: %u\n",
        (unsigned)received
    );


    printf(
        "[KEYBOARD RAW TEST] "
        "dropped: %u\n",
        (unsigned)
            keyboard_dropped_scancodes()
    );


    if (keyboard_dropped_scancodes() != 0)
    {
        printf(
            "[KEYBOARD RAW TEST] "
            "FAIL: input bytes were dropped\n"
        );

        return;
    }


    printf(
        "[KEYBOARD RAW TEST] PASS\n"
    );
}