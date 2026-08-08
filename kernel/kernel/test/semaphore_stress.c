#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/semaphore.h>
#include <kernel/task.h>

#include "../arch/i386/interrupts.h"


#define SEMAPHORE_STRESS_TASKS       8u
#define SEMAPHORE_STRESS_ITERATIONS  3000u

/*
 * Maximum number of workers allowed inside the simulated resource
 * simultaneously.
 */
#define SEMAPHORE_STRESS_CAPACITY    3u


static semaphore_t stress_semaphore;


/*
 * Test bookkeeping.
 */
static volatile size_t ready_tasks = 0;
static volatile size_t finished_tasks = 0;

static volatile bool start_test = false;
static volatile bool test_failed = false;


/*
 * Number of workers currently inside the simulated resource.
 *
 * This variable is TEST instrumentation. Its updates are protected
 * by temporary interrupt disabling because multiple semaphore holders
 * are intentionally allowed at the same time.
 */
static volatile size_t inside_resource = 0;


/*
 * Highest number of simultaneous holders observed.
 */
static volatile size_t maximum_inside = 0;


/*
 * Number of completed resource operations.
 */
static volatile uint32_t completed_operations = 0;


/*
 * Tiny helper for test-only counters.
 */
static void test_increment(
    volatile size_t *counter)
{
    uint32_t flags =
        interrupt_save_disable();

    ++(*counter);

    interrupt_restore(flags);
}


/*
 * Record entry into the simulated limited-capacity resource.
 */
static void record_resource_entry(
    size_t task_index)
{
    uint32_t flags =
        interrupt_save_disable();


    ++inside_resource;


    if (inside_resource >
        maximum_inside)
    {
        maximum_inside =
            inside_resource;
    }


    /*
     * This is the central semaphore property being tested.
     *
     * The semaphore has only SEMAPHORE_STRESS_CAPACITY permits.
     */
    if (inside_resource >
        SEMAPHORE_STRESS_CAPACITY)
    {
        printf(
            "[SEMAPHORE STRESS] CAPACITY FAILURE "
            "task=%u inside=%u capacity=%u\n",
            (unsigned)(task_index + 1),
            (unsigned)inside_resource,
            (unsigned)SEMAPHORE_STRESS_CAPACITY
        );

        test_failed = true;
    }


    interrupt_restore(flags);
}


/*
 * Record exit from the simulated resource.
 */
static void record_resource_exit(void)
{
    uint32_t flags =
        interrupt_save_disable();


    if (inside_resource == 0)
    {
        printf(
            "[SEMAPHORE STRESS] internal test "
            "underflow\n"
        );

        test_failed = true;
    }
    else
    {
        --inside_resource;
    }


    ++completed_operations;


    interrupt_restore(flags);
}


static void semaphore_stress_worker(
    void *argument)
{
    size_t task_index =
        (size_t)(uintptr_t)argument;


    test_increment(
        &ready_tasks
    );


    /*
     * Startup barrier.
     */
    while (!start_test)
        task_yield();


    for (size_t iteration = 0;
         iteration < SEMAPHORE_STRESS_ITERATIONS;
         ++iteration)
    {
        /*
         * ----------------------------------------------------------
         * ACQUIRE ONE PERMIT
         * ----------------------------------------------------------
         */
        if (!semaphore_wait(
                &stress_semaphore))
        {
            printf(
                "[SEMAPHORE STRESS] task %u "
                "wait failed\n",
                (unsigned)(task_index + 1)
            );

            test_failed = true;

            break;
        }


        /*
         * We now own one of the three permits.
         */
        record_resource_entry(
            task_index
        );


        /*
         * ----------------------------------------------------------
         * SIMULATED RESOURCE USAGE
         * ----------------------------------------------------------
         *
         * Yield while retaining the semaphore permit.
         *
         * This is intentional.
         *
         * With capacity 3:
         *
         *     three tasks may get permits
         *
         * while:
         *
         *     the remaining five must eventually block.
         */
        task_yield();
        task_yield();


        /*
         * The permit is still held here.
         */
        record_resource_exit();


        /*
         * ----------------------------------------------------------
         * RETURN PERMIT
         * ----------------------------------------------------------
         */
        if (!semaphore_signal(
                &stress_semaphore))
        {
            printf(
                "[SEMAPHORE STRESS] task %u "
                "signal failed\n",
                (unsigned)(task_index + 1)
            );

            test_failed = true;

            break;
        }


        /*
         * Allow a woken waiter to run.
         */
        task_yield();


        if (test_failed)
            break;
    }


    printf(
        "[SEMAPHORE STRESS] task %u finished\n",
        (unsigned)(task_index + 1)
    );


    test_increment(
        &finished_tasks
    );


    task_exit();
}


void semaphore_stress_test(void)
{
    printf(
        "\n[SEMAPHORE STRESS] starting\n"
    );


    semaphore_initialize(
        &stress_semaphore,
        SEMAPHORE_STRESS_CAPACITY
    );


    ready_tasks = 0;
    finished_tasks = 0;

    start_test = false;
    test_failed = false;

    inside_resource = 0;
    maximum_inside = 0;
    completed_operations = 0;


    /*
     * --------------------------------------------------------------
     * BASIC API TESTS
     * --------------------------------------------------------------
     */

    if (semaphore_get_count(
            &stress_semaphore)
        != SEMAPHORE_STRESS_CAPACITY)
    {
        printf(
            "[SEMAPHORE STRESS] FAIL: "
            "wrong initial count\n"
        );

        return;
    }


    /*
     * Consume all three permits without blocking.
     */
    for (size_t i = 0;
         i < SEMAPHORE_STRESS_CAPACITY;
         ++i)
    {
        if (!semaphore_try_wait(
                &stress_semaphore))
        {
            printf(
                "[SEMAPHORE STRESS] FAIL: "
                "try_wait failed at permit %u\n",
                (unsigned)i
            );

            return;
        }
    }


    /*
     * No permits should remain.
     */
    if (semaphore_get_count(
            &stress_semaphore) != 0)
    {
        printf(
            "[SEMAPHORE STRESS] FAIL: "
            "count not zero after consuming permits\n"
        );

        return;
    }


    /*
     * This must fail rather than block.
     */
    if (semaphore_try_wait(
            &stress_semaphore))
    {
        printf(
            "[SEMAPHORE STRESS] FAIL: "
            "try_wait succeeded with count zero\n"
        );

        return;
    }


    /*
     * Restore all permits.
     */
    for (size_t i = 0;
         i < SEMAPHORE_STRESS_CAPACITY;
         ++i)
    {
        if (!semaphore_signal(
                &stress_semaphore))
        {
            printf(
                "[SEMAPHORE STRESS] FAIL: "
                "signal failed during basic test\n"
            );

            return;
        }
    }


    if (semaphore_get_count(
            &stress_semaphore)
        != SEMAPHORE_STRESS_CAPACITY)
    {
        printf(
            "[SEMAPHORE STRESS] FAIL: "
            "count not restored\n"
        );

        return;
    }


    printf(
        "[SEMAPHORE STRESS] basic checks passed\n"
    );


    /*
     * --------------------------------------------------------------
     * CREATE WORKERS
     * --------------------------------------------------------------
     */
    for (size_t i = 0;
         i < SEMAPHORE_STRESS_TASKS;
         ++i)
    {
        task_t *task =
            task_create_kernel(
                semaphore_stress_worker,
                (void *)(uintptr_t)i
            );


        if (task == NULL)
        {
            printf(
                "[SEMAPHORE STRESS] FAIL: "
                "could not create task %u\n",
                (unsigned)(i + 1)
            );

            return;
        }
    }


    /*
     * Wait until every worker exists and is ready.
     */
    while (ready_tasks <
           SEMAPHORE_STRESS_TASKS)
    {
        task_yield();
    }


    printf(
        "[SEMAPHORE STRESS] workers ready\n"
    );


    start_test = true;


    /*
     * Wait for every worker.
     */
    while (finished_tasks <
           SEMAPHORE_STRESS_TASKS)
    {
        task_yield();
    }


    /*
     * --------------------------------------------------------------
     * FINAL VERIFICATION
     * --------------------------------------------------------------
     */

    uint32_t expected_operations =
        SEMAPHORE_STRESS_TASKS *
        SEMAPHORE_STRESS_ITERATIONS;


    printf(
        "[SEMAPHORE STRESS] operations: "
        "%u expected: %u\n",
        (unsigned)completed_operations,
        (unsigned)expected_operations
    );


    printf(
        "[SEMAPHORE STRESS] maximum inside: "
        "%u capacity: %u\n",
        (unsigned)maximum_inside,
        (unsigned)SEMAPHORE_STRESS_CAPACITY
    );


    printf(
        "[SEMAPHORE STRESS] final count: %u\n",
        (unsigned)semaphore_get_count(
            &stress_semaphore)
    );


    if (completed_operations !=
        expected_operations)
    {
        printf(
            "[SEMAPHORE STRESS] FAIL: "
            "operation count incorrect\n"
        );

        test_failed = true;
    }


    if (maximum_inside >
        SEMAPHORE_STRESS_CAPACITY)
    {
        printf(
            "[SEMAPHORE STRESS] FAIL: "
            "capacity exceeded\n"
        );

        test_failed = true;
    }


    /*
     * We deliberately expect the stress test to reach full capacity.
     *
     * With eight heavily yielding workers and three permits this
     * should happen reliably.
     */
    if (maximum_inside !=
        SEMAPHORE_STRESS_CAPACITY)
    {
        printf(
            "[SEMAPHORE STRESS] FAIL: "
            "full capacity was never exercised\n"
        );

        test_failed = true;
    }


    if (inside_resource != 0)
    {
        printf(
            "[SEMAPHORE STRESS] FAIL: "
            "workers remain inside resource\n"
        );

        test_failed = true;
    }


    if (semaphore_get_count(
            &stress_semaphore)
        != SEMAPHORE_STRESS_CAPACITY)
    {
        printf(
            "[SEMAPHORE STRESS] FAIL: "
            "permits were lost\n"
        );

        test_failed = true;
    }


    if (test_failed)
    {
        printf(
            "[SEMAPHORE STRESS] FAIL\n"
        );
    }
    else
    {
        printf(
            "[SEMAPHORE STRESS] PASS\n"
        );
    }
}