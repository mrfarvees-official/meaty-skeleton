#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/test.h>
#include <kernel/mutex.h>
#include <kernel/task.h>

#include "../arch/i386/interrupts.h"


#define MUTEX_STRESS_TASKS       8u
#define MUTEX_STRESS_ITERATIONS  5000u


static mutex_t stress_mutex;


/*
 * Shared state deliberately protected only by stress_mutex.
 */
static volatile uint32_t shared_counter = 0;


/*
 * Used to detect more than one worker entering the protected region.
 *
 * This variable is accessed only while stress_mutex should be owned.
 */
static volatile uint32_t inside_critical_section = 0;


static volatile size_t ready_tasks = 0;
static volatile size_t finished_tasks = 0;

static volatile bool start_test = false;
static volatile bool test_failed = false;


static void increment_test_counter(
    volatile size_t *counter)
{
    uint32_t flags =
        interrupt_save_disable();

    ++(*counter);

    interrupt_restore(flags);
}


static void mutex_stress_worker(void *argument)
{
    size_t task_index =
        (size_t)(uintptr_t)argument;


    increment_test_counter(
        &ready_tasks
    );


    while (!start_test)
        task_yield();


    for (size_t iteration = 0;
         iteration < MUTEX_STRESS_ITERATIONS;
         ++iteration)
    {
        /*
         * ----------------------------------------------------------
         * ACQUIRE
         * ----------------------------------------------------------
         */
        if (!mutex_lock(&stress_mutex))
        {
            printf(
                "[MUTEX STRESS] task %u "
                "failed to acquire mutex\n",
                (unsigned)(task_index + 1)
            );

            test_failed = true;
            break;
        }


        /*
         * ----------------------------------------------------------
         * CRITICAL SECTION
         * ----------------------------------------------------------
         *
         * No two tasks may execute this section simultaneously.
         */

        ++inside_critical_section;

        if (inside_critical_section != 1u)
        {
            printf(
                "[MUTEX STRESS] MUTUAL EXCLUSION FAILURE "
                "task=%u inside=%u\n",
                (unsigned)(task_index + 1),
                (unsigned)inside_critical_section
            );

            test_failed = true;
        }


        /*
         * Read current value.
         */
        uint32_t value =
            shared_counter;


        /*
         * This is INTENTIONAL.
         *
         * We yield while still holding the mutex.
         *
         * Other workers should run, attempt mutex_lock(), and become
         * blocked on the wait queue.
         *
         * This strongly exercises blocking/wakeup behavior.
         */
        task_yield();


        /*
         * If mutual exclusion works, nobody could have modified
         * shared_counter while this task was yielded.
         */
        shared_counter =
            value + 1u;


        --inside_critical_section;


        /*
         * ----------------------------------------------------------
         * RELEASE
         * ----------------------------------------------------------
         */
        if (!mutex_unlock(&stress_mutex))
        {
            printf(
                "[MUTEX STRESS] task %u "
                "failed to unlock mutex\n",
                (unsigned)(task_index + 1)
            );

            test_failed = true;
            break;
        }


        /*
         * Give another task an opportunity to run after unlocking.
         */
        task_yield();


        if (test_failed)
            break;
    }


    printf(
        "[MUTEX STRESS] task %u finished\n",
        (unsigned)(task_index + 1)
    );


    increment_test_counter(
        &finished_tasks
    );


    task_exit();
}


void mutex_stress_test(void)
{
    printf(
        "\n[MUTEX STRESS] starting\n"
    );


    mutex_initialize(
        &stress_mutex
    );


    shared_counter = 0;
    inside_critical_section = 0;

    ready_tasks = 0;
    finished_tasks = 0;

    start_test = false;
    test_failed = false;


    /*
     * --------------------------------------------------------------
     * BASIC API CHECKS
     * --------------------------------------------------------------
     */

    if (mutex_is_locked(&stress_mutex))
    {
        printf(
            "[MUTEX STRESS] FAIL: "
            "new mutex reports locked\n"
        );

        return;
    }


    if (!mutex_try_lock(&stress_mutex))
    {
        printf(
            "[MUTEX STRESS] FAIL: "
            "initial try_lock failed\n"
        );

        return;
    }


    if (!mutex_is_locked(&stress_mutex))
    {
        printf(
            "[MUTEX STRESS] FAIL: "
            "mutex not locked after try_lock\n"
        );

        return;
    }


    /*
     * Recursive acquisition is forbidden.
     */
    if (mutex_try_lock(&stress_mutex))
    {
        printf(
            "[MUTEX STRESS] FAIL: "
            "recursive try_lock succeeded\n"
        );

        mutex_unlock(&stress_mutex);

        return;
    }


    if (!mutex_unlock(&stress_mutex))
    {
        printf(
            "[MUTEX STRESS] FAIL: "
            "basic unlock failed\n"
        );

        return;
    }


    if (mutex_is_locked(&stress_mutex))
    {
        printf(
            "[MUTEX STRESS] FAIL: "
            "mutex remained locked\n"
        );

        return;
    }


    printf(
        "[MUTEX STRESS] basic checks passed\n"
    );


    /*
     * --------------------------------------------------------------
     * CREATE WORKERS
     * --------------------------------------------------------------
     */
    for (size_t i = 0;
         i < MUTEX_STRESS_TASKS;
         ++i)
    {
        task_t *task =
            task_create_kernel(
                mutex_stress_worker,
                (void *)(uintptr_t)i
            );

        if (task == NULL)
        {
            printf(
                "[MUTEX STRESS] FAIL: "
                "could not create task %u\n",
                (unsigned)(i + 1)
            );

            return;
        }
    }


    /*
     * Ensure all workers exist before releasing the barrier.
     */
    while (ready_tasks <
           MUTEX_STRESS_TASKS)
    {
        task_yield();
    }


    printf(
        "[MUTEX STRESS] workers ready\n"
    );


    start_test = true;


    /*
     * Wait for all workers to complete.
     */
    while (finished_tasks <
           MUTEX_STRESS_TASKS)
    {
        task_yield();
    }


    /*
     * --------------------------------------------------------------
     * VERIFY RESULT
     * --------------------------------------------------------------
     */

    uint32_t expected =
        MUTEX_STRESS_TASKS *
        MUTEX_STRESS_ITERATIONS;


    printf(
        "[MUTEX STRESS] counter: "
        "%u expected: %u\n",
        (unsigned)shared_counter,
        (unsigned)expected
    );


    if (shared_counter != expected)
    {
        printf(
            "[MUTEX STRESS] FAIL: "
            "shared counter incorrect\n"
        );

        test_failed = true;
    }


    if (inside_critical_section != 0)
    {
        printf(
            "[MUTEX STRESS] FAIL: "
            "critical-section count=%u\n",
            (unsigned)inside_critical_section
        );

        test_failed = true;
    }


    if (mutex_is_locked(&stress_mutex))
    {
        printf(
            "[MUTEX STRESS] FAIL: "
            "mutex left locked\n"
        );

        test_failed = true;
    }


    if (test_failed)
    {
        printf(
            "[MUTEX STRESS] FAIL\n"
        );
    }
    else
    {
        printf(
            "[MUTEX STRESS] PASS\n"
        );
    }
}