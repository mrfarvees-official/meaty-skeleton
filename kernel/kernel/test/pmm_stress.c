#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/test.h>
#include <kernel/pmm.h>
#include <kernel/task.h>

#include "../arch/i386/interrupts.h"

#define PMM_STRESS_TASKS       4
#define PMM_STRESS_ITERATIONS  5000
#define PMM_STRESS_BATCH       16


/*
 * Used as a simple startup barrier.
 *
 * Interrupts are disabled while modifying counters because we're
 * currently testing the PMM lock itself and don't want to introduce
 * another synchronization primitive into the test.
 */
static volatile size_t ready_tasks = 0;
static volatile size_t finished_tasks = 0;

static volatile bool start_test = false;
static volatile bool test_failed = false;


/*
 * Each task publishes its currently allocated frames here.
 *
 * This allows us to check that two tasks were never given the same
 * physical frame.
 */
static volatile uintptr_t
    allocated_frames[PMM_STRESS_TASKS][PMM_STRESS_BATCH];


static void increment_counter(volatile size_t *counter)
{
    uint32_t flags = interrupt_save_disable();

    ++(*counter);

    interrupt_restore(flags);
}


static bool check_for_duplicates(void)
{
    /*
     * The workers only modify their own row while running.
     *
     * Disable interrupts so no worker can modify the table while
     * we're checking it on this single-core kernel.
     */
    uint32_t flags = interrupt_save_disable();

    for (size_t task_a = 0;
         task_a < PMM_STRESS_TASKS;
         ++task_a)
    {
        for (size_t frame_a = 0;
             frame_a < PMM_STRESS_BATCH;
             ++frame_a)
        {
            uintptr_t a =
                allocated_frames[task_a][frame_a];

            if (a == 0)
                continue;

            for (size_t task_b = task_a;
                 task_b < PMM_STRESS_TASKS;
                 ++task_b)
            {
                size_t start_frame = 0;

                if (task_b == task_a)
                    start_frame = frame_a + 1;

                for (size_t frame_b = start_frame;
                     frame_b < PMM_STRESS_BATCH;
                     ++frame_b)
                {
                    uintptr_t b =
                        allocated_frames[task_b][frame_b];

                    if (b == 0)
                        continue;

                    if (a == b)
                    {
                        printf(
                            "[PMM STRESS] DUPLICATE FRAME "
                            "0x%08x\n",
                            (unsigned)a
                        );

                        printf(
                            "[PMM STRESS] owners: "
                            "task %u slot %u and "
                            "task %u slot %u\n",
                            (unsigned)(task_a + 1),
                            (unsigned)frame_a,
                            (unsigned)(task_b + 1),
                            (unsigned)frame_b
                        );

                        interrupt_restore(flags);

                        return false;
                    }
                }
            }
        }
    }

    interrupt_restore(flags);

    return true;
}


static void pmm_stress_worker(void *argument)
{
    size_t task_index =
        (size_t)(uintptr_t)argument;

    /*
     * Tell the controller that this task exists and has reached
     * the starting barrier.
     *
     * At this point its stack and task_t have already been allocated.
     */
    increment_counter(&ready_tasks);

    /*
     * Don't perform any PMM testing until the controller has recorded
     * the baseline.
     */
    while (!start_test)
        task_yield();


    for (size_t iteration = 0;
         iteration < PMM_STRESS_ITERATIONS;
         ++iteration)
    {
        /*
         * Clear this task's published frame array.
         */
        for (size_t i = 0;
             i < PMM_STRESS_BATCH;
             ++i)
        {
            allocated_frames[task_index][i] = 0;
        }


        /*
         * Allocate several frames.
         */
        for (size_t i = 0;
             i < PMM_STRESS_BATCH;
             ++i)
        {
            uintptr_t frame =
                pmm_allocate_frame();

            if (frame == 0)
            {
                printf(
                    "[PMM STRESS] task %u "
                    "allocation failed "
                    "iteration=%u slot=%u\n",
                    (unsigned)(task_index + 1),
                    (unsigned)iteration,
                    (unsigned)i
                );

                test_failed = true;
                break;
            }

            allocated_frames[task_index][i] =
                frame;

            /*
             * Force scheduling pressure.
             */
            task_yield();
        }


        /*
         * Occasionally perform a global duplicate check.
         *
         * Don't do this every iteration because the check itself
         * is relatively expensive.
         */
        if ((iteration % 100u) == 0)
        {
            if (!check_for_duplicates())
            {
                test_failed = true;
            }
        }


        /*
         * Free even slots first.
         */
        for (size_t i = 0;
             i < PMM_STRESS_BATCH;
             i += 2)
        {
            uintptr_t frame =
                allocated_frames[task_index][i];

            if (frame != 0)
            {
                /*
                 * Clear publication before returning the frame
                 * to the PMM.
                 */
                allocated_frames[task_index][i] = 0;

                pmm_free_frame(frame);
            }

            task_yield();
        }


        /*
         * Then free odd slots.
         */
        for (size_t i = 1;
             i < PMM_STRESS_BATCH;
             i += 2)
        {
            uintptr_t frame =
                allocated_frames[task_index][i];

            if (frame != 0)
            {
                allocated_frames[task_index][i] = 0;

                pmm_free_frame(frame);
            }

            task_yield();
        }


        if (test_failed)
            break;
    }


    /*
     * Make absolutely sure this worker has no frames left allocated.
     */
    for (size_t i = 0;
         i < PMM_STRESS_BATCH;
         ++i)
    {
        uintptr_t frame =
            allocated_frames[task_index][i];

        if (frame != 0)
        {
            allocated_frames[task_index][i] = 0;

            pmm_free_frame(frame);
        }
    }


    printf(
        "[PMM STRESS] task %u finished\n",
        (unsigned)(task_index + 1)
    );

    increment_counter(&finished_tasks);

    task_exit();
}


void pmm_stress_test(void)
{
    printf("\n[PMM STRESS] starting\n");

    ready_tasks = 0;
    finished_tasks = 0;
    start_test = false;
    test_failed = false;


    /*
     * Clear published frame table.
     */
    for (size_t task = 0;
         task < PMM_STRESS_TASKS;
         ++task)
    {
        for (size_t frame = 0;
             frame < PMM_STRESS_BATCH;
             ++frame)
        {
            allocated_frames[task][frame] = 0;
        }
    }


    /*
     * IMPORTANT:
     *
     * Create workers BEFORE measuring PMM free frames.
     *
     * task_create_kernel() allocates task structures and 16 KiB
     * stacks from your heap.
     */
    for (size_t i = 0;
         i < PMM_STRESS_TASKS;
         ++i)
    {
        task_t *task =
            task_create_kernel(
                pmm_stress_worker,
                (void *)(uintptr_t)i
            );

        if (task == NULL)
        {
            printf(
                "[PMM STRESS] failed to create task %u\n",
                (unsigned)(i + 1)
            );

            test_failed = true;
            return;
        }
    }


    /*
     * Let every worker run until it reaches the startup barrier.
     */
    while (ready_tasks < PMM_STRESS_TASKS)
        task_yield();


    /*
     * NOW take the baseline.
     *
     * Worker stacks and task structures already exist.
     */
    size_t before =
        pmm_get_free_frame_count();

    printf(
        "[PMM STRESS] workers ready\n"
    );

    printf(
        "[PMM STRESS] free frames before: %u\n",
        (unsigned)before
    );


    /*
     * Release all workers.
     */
    start_test = true;


    while (finished_tasks < PMM_STRESS_TASKS)
        task_yield();


    size_t after =
        pmm_get_free_frame_count();

    printf(
        "[PMM STRESS] free frames after:  %u\n",
        (unsigned)after
    );


    if (before != after)
    {
        printf(
            "[PMM STRESS] FAIL: PMM leaked frames "
            "(before=%u after=%u difference=%d)\n",
            (unsigned)before,
            (unsigned)after,
            (int)before - (int)after
        );

        test_failed = true;
    }


    if (test_failed)
    {
        printf("[PMM STRESS] FAIL\n");
    }
    else
    {
        printf("[PMM STRESS] PASS\n");
    }
}