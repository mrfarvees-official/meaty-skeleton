#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <kernel/test.h>
#include <kernel/keyboard.h>
#include <kernel/tty.h>


#define LINE_TEST_BUFFER_SIZE 32u


/*
 * ==========================================================================
 * TEST HELPERS
 * ==========================================================================
 */

static bool line_test_read(
    const char *prompt,
    char *buffer,
    size_t capacity,
    size_t *length)
{
    terminal_writestring(
        prompt
    );


    return keyboard_read_line(
        buffer,
        capacity,
        length,
        true
    );
}


/*
 * ==========================================================================
 * LINE INPUT TEST
 * ==========================================================================
 */

void keyboard_line_test(void)
{
    char line[
        LINE_TEST_BUFFER_SIZE
    ];

    size_t length;


    printf(
        "\n[KEYBOARD LINE TEST] starting\n"
    );


    printf(
        "[KEYBOARD LINE TEST] "
        "buffer capacity: %u "
        "(maximum text length: %u)\n",
        (unsigned)LINE_TEST_BUFFER_SIZE,
        (unsigned)(
            LINE_TEST_BUFFER_SIZE - 1u
        )
    );


    /*
     * ======================================================================
     * TEST 1: NORMAL INPUT
     * ======================================================================
     */

    printf(
        "\n[TEST 1] normal input\n"
    );

    printf(
        "[TEST 1] type: hello world\n"
    );


    if (!line_test_read(
            "> ",
            line,
            sizeof(line),
            &length))
    {
        printf(
            "[TEST 1] FAIL: "
            "keyboard_read_line failed\n"
        );

        return;
    }


    printf(
        "[TEST 1] line=\"%s\" "
        "length=%u\n",
        line,
        (unsigned)length
    );


    if (strcmp(
            line,
            "hello world") != 0)
    {
        printf(
            "[TEST 1] FAIL: "
            "expected \"hello world\"\n"
        );

        return;
    }


    if (length !=
        strlen("hello world"))
    {
        printf(
            "[TEST 1] FAIL: "
            "wrong length\n"
        );

        return;
    }


    printf(
        "[TEST 1] PASS\n"
    );


    /*
     * ======================================================================
     * TEST 2: BACKSPACE
     * ======================================================================
     */

    printf(
        "\n[TEST 2] Backspace editing\n"
    );

    printf(
        "[TEST 2] type: hellp"
        ", Backspace, o, Enter\n"
    );


    if (!line_test_read(
            "> ",
            line,
            sizeof(line),
            &length))
    {
        printf(
            "[TEST 2] FAIL\n"
        );

        return;
    }


    printf(
        "[TEST 2] line=\"%s\"\n",
        line
    );


    if (strcmp(
            line,
            "hello") != 0)
    {
        printf(
            "[TEST 2] FAIL: "
            "expected \"hello\"\n"
        );

        return;
    }


    printf(
        "[TEST 2] PASS\n"
    );


    /*
     * ======================================================================
     * TEST 3: EMPTY BACKSPACE
     * ======================================================================
     */

    printf(
        "\n[TEST 3] empty-line Backspace\n"
    );

    printf(
        "[TEST 3] press Backspace several times, "
        "then type: ok\n"
    );


    if (!line_test_read(
            "> ",
            line,
            sizeof(line),
            &length))
    {
        printf(
            "[TEST 3] FAIL\n"
        );

        return;
    }


    if (strcmp(
            line,
            "ok") != 0)
    {
        printf(
            "[TEST 3] FAIL: "
            "expected \"ok\"\n"
        );

        return;
    }


    printf(
        "[TEST 3] PASS\n"
    );


    /*
     * ======================================================================
     * TEST 4: SHIFT / CAPS / PUNCTUATION
     * ======================================================================
     */

    printf(
        "\n[TEST 4] translated characters\n"
    );

    printf(
        "[TEST 4] type exactly: Hello, World!\n"
    );


    if (!line_test_read(
            "> ",
            line,
            sizeof(line),
            &length))
    {
        printf(
            "[TEST 4] FAIL\n"
        );

        return;
    }


    printf(
        "[TEST 4] line=\"%s\"\n",
        line
    );


    if (strcmp(
            line,
            "Hello, World!") != 0)
    {
        printf(
            "[TEST 4] FAIL: "
            "expected \"Hello, World!\"\n"
        );

        return;
    }


    printf(
        "[TEST 4] PASS\n"
    );


    /*
     * ======================================================================
     * TEST 5: FULL BUFFER
     * ======================================================================
     *
     * Use a deliberately tiny buffer.
     */

    {
        char tiny[8];

        size_t tiny_length;


        printf(
            "\n[TEST 5] bounded buffer\n"
        );

        printf(
            "[TEST 5] type more than 7 characters, "
            "then Enter\n"
        );

        printf(
            "[TEST 5] only the first 7 should "
            "appear and be stored\n"
        );


        if (!line_test_read(
                "> ",
                tiny,
                sizeof(tiny),
                &tiny_length))
        {
            printf(
                "[TEST 5] FAIL\n"
            );

            return;
        }


        printf(
            "[TEST 5] line=\"%s\" "
            "length=%u\n",
            tiny,
            (unsigned)tiny_length
        );


        if (tiny_length >
            sizeof(tiny) - 1u)
        {
            printf(
                "[TEST 5] FAIL: "
                "buffer overflow detected\n"
            );

            return;
        }


        if (tiny[
                sizeof(tiny) - 1u
            ] != '\0')
        {
            printf(
                "[TEST 5] FAIL: "
                "missing terminator\n"
            );

            return;
        }


        printf(
            "[TEST 5] PASS\n"
        );
    }


    /*
     * ======================================================================
     * TEST 6: EMPTY LINE
     * ======================================================================
     */

    printf(
        "\n[TEST 6] empty line\n"
    );

    printf(
        "[TEST 6] press Enter immediately\n"
    );


    if (!line_test_read(
            "> ",
            line,
            sizeof(line),
            &length))
    {
        printf(
            "[TEST 6] FAIL\n"
        );

        return;
    }


    if (length != 0 ||
        line[0] != '\0')
    {
        printf(
            "[TEST 6] FAIL: "
            "expected empty string\n"
        );

        return;
    }


    printf(
        "[TEST 6] PASS\n"
    );


    /*
     * ======================================================================
     * TEST 7: CONSECUTIVE LINES
     * ======================================================================
     */

    printf(
        "\n[TEST 7] consecutive lines\n"
    );


    printf(
        "[TEST 7] type: first\n"
    );


    if (!line_test_read(
            "first> ",
            line,
            sizeof(line),
            &length))
    {
        printf(
            "[TEST 7] FAIL\n"
        );

        return;
    }


    if (strcmp(
            line,
            "first") != 0)
    {
        printf(
            "[TEST 7] FAIL: "
            "first line incorrect\n"
        );

        return;
    }


    printf(
        "[TEST 7] type: second\n"
    );


    if (!line_test_read(
            "second> ",
            line,
            sizeof(line),
            &length))
    {
        printf(
            "[TEST 7] FAIL\n"
        );

        return;
    }


    if (strcmp(
            line,
            "second") != 0)
    {
        printf(
            "[TEST 7] FAIL: "
            "second line incorrect\n"
        );

        return;
    }


    printf(
        "[TEST 7] PASS\n"
    );


    /*
     * ======================================================================
     * FINAL
     * ======================================================================
     */

    printf(
        "\n[KEYBOARD LINE TEST] PASS\n"
    );
}