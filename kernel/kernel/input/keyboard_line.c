#include <stdbool.h>
#include <stddef.h>

#include <kernel/keyboard.h>
#include <kernel/tty.h>


/*
 * ==========================================================================
 * LINE INPUT
 * ==========================================================================
 *
 * This layer sits above:
 *
 *     keyboard_wait_character()
 *
 * It does NOT interact with:
 *
 *     IRQ1
 *     scancodes
 *     scheduler internals
 *     semaphores directly
 *
 * Those details belong to the keyboard driver.
 */


/*
 * Characters accepted as ordinary line contents.
 *
 * For this beginner-stage line editor we accept printable ASCII:
 *
 *     0x20 .. 0x7E
 *
 * This includes:
 *
 *     letters
 *     numbers
 *     spaces
 *     punctuation
 *
 * Control characters such as Tab are deliberately ignored.
 */
static bool line_character_is_printable(
    char character)
{
    unsigned char value =
        (unsigned char)character;

    return
        value >= 0x20u &&
        value <= 0x7Eu;
}


bool keyboard_read_line(
    char *buffer,
    size_t capacity,
    size_t *length,
    bool echo)
{
    /*
     * We need at least one byte for the terminating '\0'.
     */
    if (buffer == NULL ||
        capacity == 0)
    {
        return false;
    }


    size_t position = 0;


    /*
     * Maintain a valid C string throughout editing.
     */
    buffer[0] = '\0';


    for (;;)
    {
        char character;


        /*
         * --------------------------------------------------------------
         * BLOCKING INPUT
         * --------------------------------------------------------------
         *
         * No polling.
         *
         * If no character exists, keyboard_wait_character() blocks the
         * current task through the semaphore/wait-queue infrastructure.
         */
        if (!keyboard_wait_character(
                &character))
        {
            buffer[position] =
                '\0';

            if (length != NULL)
                *length = position;

            return false;
        }


        /*
         * --------------------------------------------------------------
         * ENTER
         * --------------------------------------------------------------
         *
         * keyboard translation gives us '\n' for Enter.
         *
         * Enter terminates the line but is NOT stored in the buffer.
         */
        if (character == '\n')
        {
            buffer[position] =
                '\0';


            if (echo)
            {
                terminal_putchar(
                    '\n'
                );
            }


            if (length != NULL)
            {
                *length =
                    position;
            }


            return true;
        }


        /*
         * --------------------------------------------------------------
         * BACKSPACE
         * --------------------------------------------------------------
         *
         * Backspace only has an effect when the editable buffer contains
         * at least one character.
         */
        if (character == '\b')
        {
            if (position == 0)
            {
                /*
                 * Nothing to erase.
                 *
                 * Most importantly, do NOT emit '\b' here, otherwise the
                 * cursor could move backward into the prompt.
                 */
                continue;
            }


            --position;


            /*
             * Keep the buffer terminated after every edit.
             */
            buffer[position] =
                '\0';


            if (echo)
            {
                /*
                 * Your terminal_putchar('\b') already:
                 *
                 *     1. moves the cursor backward
                 *     2. writes a space into that cell
                 *
                 * Therefore DO NOT use:
                 *
                 *     '\b', ' ', '\b'
                 *
                 * with your terminal implementation.
                 */
                terminal_putchar(
                    '\b'
                );
            }


            continue;
        }


        /*
         * --------------------------------------------------------------
         * OTHER CONTROL CHARACTERS
         * --------------------------------------------------------------
         *
         * Tab is intentionally ignored for this first line editor.
         *
         * Arrow keys never reach the character queue anyway.
         */
        if (!line_character_is_printable(
                character))
        {
            continue;
        }


        /*
         * --------------------------------------------------------------
         * FULL BUFFER
         * --------------------------------------------------------------
         *
         * Always reserve one byte for:
         *
         *     '\0'
         *
         * capacity = 8 therefore permits:
         *
         *     7 characters + '\0'
         */
        if (position >=
            capacity - 1u)
        {
            /*
             * Buffer full.
             *
             * Ignore further printable characters until the user either:
             *
             *     - presses Backspace and makes room
             *     - presses Enter and submits the line
             *
             * We deliberately do NOT echo rejected characters because the
             * visible line must remain identical to buffer contents.
             */
            continue;
        }


        /*
         * --------------------------------------------------------------
         * APPEND CHARACTER
         * --------------------------------------------------------------
         */

        buffer[position] =
            character;

        ++position;


        /*
         * Maintain C-string validity.
         */
        buffer[position] =
            '\0';


        if (echo)
        {
            terminal_putchar(
                character
            );
        }
    }
}