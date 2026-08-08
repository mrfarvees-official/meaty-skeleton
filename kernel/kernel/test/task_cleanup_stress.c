#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/test.h>
#include <kernel/pmm.h>
#include <kernel/task.h>

#include "../arch/i386/interrupts.h"


#define CLEANUP_STRESS_ROUNDS          20u
#define CLEANUP_STRESS_TASKS_PER_ROUND 32u


/*
 * Worker completion count.
 *
 * This tells the controller that worker code reached its end.
 *
 * It does NOT mean the task has already been reaped.
 */
static volatile size_t workers_finished = 0;


/*
 * General test failure flag.
 */
static volatile bool test_failed = false;


/*
 * Test-only counter update.
 */
static void cleanup_test_increment(
    volatile size_t *counter)
{
    uint32_t flags =
        interrupt_save_disable();

    ++(*counter);

    interrupt_restore(flags);
}


/*
 * Very short-lived task.
 *
 * Returning automatically enters task_exit() through task_bootstrap().
 */
static void cleanup_stress_worker(void *argument)
{
    uintptr_t worker_number =
        (uintptr_t)argument;


    /*
     * Perform a little stack activity so this isn't a completely
     * empty thread.
     */
    volatile uint32_t values[16];

    for (size_t i = 0;
         i < 16u;
         ++i)
    {
        values[i] =
            (uint32_t)(
                worker_number +
                i
            );
    }


    /*
     * Give other tasks opportunities to run.
     */
    task_yield();


    /*
     * Prevent the compiler from considering the array irrelevant.
     */
    if (values[0] ==
        0xFFFFFFFFu)
    {
        test_failed = true;
    }


    cleanup_test_increment(
        &workers_finished
    );


    /*
     * Deliberately return instead of explicitly calling task_exit().
     *
     * task_bootstrap() must translate a normal thread return into
     * task_exit().
     */
}


/*
 * Wait until the reaper has destroyed a specific number of tasks.
 */
static void wait_for_reaped_total(
    uint64_t target)
{
    while (task_cleanup_total_reaped() <
           target)
    {
        task_yield();
    }
}


void task_cleanup_stress_test(void)
{
    printf(
        "\n[TASK CLEANUP] starting\n"
    );


    test_failed = false;


    /*
     * Reaper itself exists permanently and does not count here.
     */
    uint64_t initial_reaped =
        task_cleanup_total_reaped();


    printf(
        "[TASK CLEANUP] initial reaped: %llu\n",
        (unsigned long long)initial_reaped
    );


    /*
     * --------------------------------------------------------------
     * WARM-UP ROUND
     * --------------------------------------------------------------
     *
     * The first batch may expand the heap because task stacks and
     * task_t objects need storage.
     *
     * Your heap retains mapped pages, so PMM free-frame count may
     * decrease during this first batch even though all task allocations
     * are later kfree()'d.
     *
     * After cleanup, those free heap blocks should be reused by later
     * batches.
     */

    workers_finished = 0;


    uint64_t warmup_target =
        task_cleanup_total_reaped() +
        CLEANUP_STRESS_TASKS_PER_ROUND;


    for (size_t i = 0;
         i < CLEANUP_STRESS_TASKS_PER_ROUND;
         ++i)
    {
        task_t *task =
            task_create_kernel(
                cleanup_stress_worker,
                (void *)(uintptr_t)i
            );


        if (task == NULL)
        {
            printf(
                "[TASK CLEANUP] FAIL: "
                "warm-up task creation failed at %u\n",
                (unsigned)i
            );

            test_failed = true;
            break;
        }
    }


    if (test_failed)
    {
        printf(
            "[TASK CLEANUP] FAIL\n"
        );

        return;
    }


    while (workers_finished <
           CLEANUP_STRESS_TASKS_PER_ROUND)
    {
        task_yield();
    }


    /*
     * Workers have finished executing, but some may still only be
     * zombies.
     *
     * Wait for actual destruction.
     */
    wait_for_reaped_total(
        warmup_target
    );


    if (task_cleanup_pending_count() != 0)
    {
        printf(
            "[TASK CLEANUP] FAIL: "
            "pending zombies remain after warm-up\n"
        );

        test_failed = true;
    }


    size_t stable_frame_baseline =
        pmm_get_free_frame_count();


    printf(
        "[TASK CLEANUP] warm-up complete\n"
    );

    printf(
        "[TASK CLEANUP] stable free frames: %u\n",
        (unsigned)stable_frame_baseline
    );


    /*
     * --------------------------------------------------------------
     * REPEATED ROUNDS
     * --------------------------------------------------------------
     *
     * Each round creates and destroys the same number of tasks.
     *
     * Because heap blocks from the warm-up should now be reusable,
     * later rounds should not require continually more physical pages.
     */

    for (size_t round = 0;
         round < CLEANUP_STRESS_ROUNDS;
         ++round)
    {
        workers_finished = 0;


        uint64_t before_reaped =
            task_cleanup_total_reaped();


        uint64_t target_reaped =
            before_reaped +
            CLEANUP_STRESS_TASKS_PER_ROUND;


        /*
         * Create one complete batch before allowing the controller
         * to consider the round complete.
         */
        for (size_t i = 0;
             i < CLEANUP_STRESS_TASKS_PER_ROUND;
             ++i)
        {
            uintptr_t worker_number =
                (uintptr_t)(
                    round *
                    CLEANUP_STRESS_TASKS_PER_ROUND +
                    i
                );


            task_t *task =
                task_create_kernel(
                    cleanup_stress_worker,
                    (void *)worker_number
                );


            if (task == NULL)
            {
                printf(
                    "[TASK CLEANUP] FAIL: "
                    "round %u creation failed "
                    "at task %u\n",
                    (unsigned)(round + 1),
                    (unsigned)i
                );

                test_failed = true;

                break;
            }
        }


        if (test_failed)
            break;


        /*
         * First wait until all worker functions have returned.
         */
        while (workers_finished <
               CLEANUP_STRESS_TASKS_PER_ROUND)
        {
            task_yield();
        }


        /*
         * Then wait for actual reaping.
         */
        wait_for_reaped_total(
            target_reaped
        );


        size_t pending =
            task_cleanup_pending_count();


        if (pending != 0)
        {
            printf(
                "[TASK CLEANUP] FAIL: "
                "round %u left %u pending zombies\n",
                (unsigned)(round + 1),
                (unsigned)pending
            );

            test_failed = true;

            break;
        }


        uint64_t after_reaped =
            task_cleanup_total_reaped();


        if (after_reaped -
            before_reaped !=
            CLEANUP_STRESS_TASKS_PER_ROUND)
        {
            printf(
                "[TASK CLEANUP] FAIL: "
                "round %u reaped wrong count "
                "(before=%llu after=%llu)\n",
                (unsigned)(round + 1),
                (unsigned long long)before_reaped,
                (unsigned long long)after_reaped
            );

            test_failed = true;

            break;
        }


        size_t free_frames =
            pmm_get_free_frame_count();


        printf(
            "[TASK CLEANUP] round %u/%u "
            "reaped=%llu pending=%u "
            "free_frames=%u\n",
            (unsigned)(round + 1),
            (unsigned)CLEANUP_STRESS_ROUNDS,
            (unsigned long long)after_reaped,
            (unsigned)pending,
            (unsigned)free_frames
        );


        /*
         * Once the heap has been warmed up, repeatedly creating and
         * reaping the same-size batches should reuse heap blocks.
         *
         * It should not continuously consume PMM frames.
         */
        if (free_frames !=
            stable_frame_baseline)
        {
            printf(
                "[TASK CLEANUP] FAIL: "
                "free-frame count drifted "
                "(baseline=%u current=%u)\n",
                (unsigned)stable_frame_baseline,
                (unsigned)free_frames
            );

            test_failed = true;

            break;
        }
    }


    /*
     * --------------------------------------------------------------
     * FINAL VERIFICATION
     * --------------------------------------------------------------
     */

    uint64_t final_reaped =
        task_cleanup_total_reaped();


    uint64_t expected_new_reaped =
        CLEANUP_STRESS_TASKS_PER_ROUND +
        (
            CLEANUP_STRESS_ROUNDS *
            CLEANUP_STRESS_TASKS_PER_ROUND
        );


    printf(
        "[TASK CLEANUP] total new reaped: "
        "%llu expected: %llu\n",
        (unsigned long long)(
            final_reaped -
            initial_reaped
        ),
        (unsigned long long)
            expected_new_reaped
    );


    printf(
        "[TASK CLEANUP] pending: %u\n",
        (unsigned)
            task_cleanup_pending_count()
    );


    if (final_reaped -
        initial_reaped !=
        expected_new_reaped)
    {
        printf(
            "[TASK CLEANUP] FAIL: "
            "final reaped count incorrect\n"
        );

        test_failed = true;
    }


    if (task_cleanup_pending_count() != 0)
    {
        printf(
            "[TASK CLEANUP] FAIL: "
            "zombies remain pending\n"
        );

        test_failed = true;
    }


    if (test_failed)
    {
        printf(
            "[TASK CLEANUP] FAIL\n"
        );
    }
    else
    {
        printf(
            "[TASK CLEANUP] PASS\n"
        );
    }
}