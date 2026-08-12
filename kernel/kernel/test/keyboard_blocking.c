#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/keyboard.h>


#define BLOCKING_EVENT_TEST_COUNT     10u
#define BLOCKING_CHARACTER_TEST_COUNT 20u


static void print_character(
    char character)
{
    if (character == '\n')
    {
        printf("\\n");
        return;
    }


    if (character == '\t')
    {
        printf("\\t");
        return;
    }


    if (character == '\b')
    {
        printf("\\b");
        return;
    }


    if (character >= 32 &&
        character <= 126)
    {
        printf(
            "'%c'",
            character
        );

        return;
    }


    printf(
        "0x%02x",
        (unsigned)(uint8_t)character
    );
}


void keyboard_blocking_test(void)
{
    printf(
        "\n[KEYBOARD BLOCKING TEST] starting\n"
    );


    /*
     * ======================================================================
     * PART 1
     * ======================================================================
     *
     * Test blocking key-event input.
     */

    printf(
        "[KEYBOARD BLOCKING TEST] "
        "event phase\n"
    );


    printf(
        "[KEYBOARD BLOCKING TEST] "
        "press/release some keys and arrows\n"
    );


    for (size_t i = 0;
         i < BLOCKING_EVENT_TEST_COUNT;
         ++i)
    {
        keyboard_event_t event;


        /*
         * NO task_yield().
         *
         * This call blocks this task until IRQ1 produces an event.
         */
        if (!keyboard_wait_event(
                &event))
        {
            printf(
                "[KEYBOARD BLOCKING TEST] "
                "FAIL: keyboard_wait_event failed\n"
            );

            return;
        }


        printf(
            "[BLOCKING EVENT] %-14s %-8s",
            keyboard_key_name(
                event.key
            ),
            event.pressed
                ? "PRESSED"
                : "RELEASED"
        );


        if (event.character != '\0')
        {
            printf(
                " char="
            );

            print_character(
                event.character
            );
        }


        printf("\n");
    }


    /*
     * ======================================================================
     * PART 2
     * ======================================================================
     *
     * The event and character queues are independent.
     *
     * Characters typed during phase 1 may therefore already exist in the
     * character queue. Drain them before beginning the dedicated character
     * test so the test prompt is easy to understand.
     */

    char character;


    while (keyboard_read_character(
               &character))
    {
        /*
         * Drain old character data.
         */
    }


    printf(
        "\n[KEYBOARD BLOCKING TEST] "
        "character phase\n"
    );


    printf(
        "[KEYBOARD BLOCKING TEST] "
        "type %u characters\n",
        (unsigned)
            BLOCKING_CHARACTER_TEST_COUNT
    );


    printf(
        "[KEYBOARD BLOCKING TEST] "
        "Shift/Caps should work. "
        "Arrow keys should NOT count.\n"
    );


    for (size_t i = 0;
         i < BLOCKING_CHARACTER_TEST_COUNT;
         ++i)
    {
        char input;


        /*
         * Again: NO polling and NO task_yield().
         */
        if (!keyboard_wait_character(
                &input))
        {
            printf(
                "\n[KEYBOARD BLOCKING TEST] "
                "FAIL: keyboard_wait_character failed\n"
            );

            return;
        }


        printf(
            "[BLOCKING CHAR] "
        );


        print_character(
            input
        );


        printf("\n");
    }


    /*
     * ======================================================================
     * FINAL DIAGNOSTICS
     * ======================================================================
     */

    printf(
        "[KEYBOARD BLOCKING TEST] "
        "pending events: %u\n",
        (unsigned)
            keyboard_pending_events()
    );


    printf(
        "[KEYBOARD BLOCKING TEST] "
        "pending characters: %u\n",
        (unsigned)
            keyboard_pending_characters()
    );


    printf(
        "[KEYBOARD BLOCKING TEST] "
        "raw dropped: %u\n",
        (unsigned)
            keyboard_dropped_scancodes()
    );


    printf(
        "[KEYBOARD BLOCKING TEST] "
        "event dropped: %u\n",
        (unsigned)
            keyboard_dropped_events()
    );


    printf(
        "[KEYBOARD BLOCKING TEST] "
        "character dropped: %u\n",
        (unsigned)
            keyboard_dropped_characters()
    );


    if (keyboard_dropped_events() != 0 ||
        keyboard_dropped_characters() != 0)
    {
        printf(
            "[KEYBOARD BLOCKING TEST] FAIL\n"
        );

        return;
    }


    printf(
        "[KEYBOARD BLOCKING TEST] PASS\n"
    );
}