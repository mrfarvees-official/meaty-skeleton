#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/test.h>
#include <kernel/paging.h>
#include <kernel/pmm.h>
#include <kernel/task.h>

#include "../arch/i386/interrupts.h"

#define PAGING_STRESS_TASKS       4
#define PAGING_STRESS_ITERATIONS  2000
#define PAGING_STRESS_PAGES       64

#define PAGING_TEST_BASE          0x40000000u

/*
 * Give each worker 4 MiB of virtual address space.
 *
 * This places each worker in a different page directory entry,
 * which also forces concurrent page-table creation.
 */
#define PAGING_WORKER_REGION_SIZE 0x00400000u

static volatile size_t ready_tasks = 0;
static volatile size_t finished_tasks = 0;

static volatile bool start_test = false;
static volatile bool test_failed = false;

static void increment_counter(
    volatile size_t *counter)
{
    uint32_t flags =
        interrupt_save_disable();

    ++(*counter);

    interrupt_restore(flags);
}

static void paging_stress_worker(void *argument)
{
    size_t task_index =
        (size_t)(uintptr_t)argument;

    uintptr_t worker_base =
        PAGING_TEST_BASE +
        task_index * PAGING_WORKER_REGION_SIZE;

    uintptr_t frames[PAGING_STRESS_PAGES];

    for (size_t i = 0;
         i < PAGING_STRESS_PAGES;
         ++i)
    {
        frames[i] = 0;
    }

    increment_counter(&ready_tasks);

    while (!start_test)
        task_yield();

    for (size_t iteration = 0;
         iteration < PAGING_STRESS_ITERATIONS;
         ++iteration)
    {
        /*
         * -------------------------
         * MAP PHASE
         * -------------------------
         */
        for (size_t i = 0;
             i < PAGING_STRESS_PAGES;
             ++i)
        {
            uintptr_t virtual_address =
                worker_base +
                i * PAGE_SIZE;

            uintptr_t frame =
                pmm_allocate_frame();

            if (frame == 0)
            {
                printf(
                    "[PAGING STRESS] task %u "
                    "PMM allocation failed\n",
                    (unsigned)(task_index + 1)
                );

                test_failed = true;
                break;
            }

            frames[i] = frame;

            bool mapped =
                paging_map_page(
                    virtual_address,
                    frame,
                    PAGE_WRITABLE
                );

            if (!mapped)
            {
                printf(
                    "[PAGING STRESS] task %u "
                    "map failed VA=0x%08x\n",
                    (unsigned)(task_index + 1),
                    (unsigned)virtual_address
                );

                pmm_free_frame(frame);
                frames[i] = 0;

                test_failed = true;
                break;
            }

            /*
             * Verify the mapping immediately.
             */
            uintptr_t resolved = 0;

            if (!paging_get_physical_address(
                    virtual_address,
                    &resolved))
            {
                printf(
                    "[PAGING STRESS] task %u "
                    "lookup failed VA=0x%08x\n",
                    (unsigned)(task_index + 1),
                    (unsigned)virtual_address
                );

                test_failed = true;
                break;
            }

            if (resolved != frame)
            {
                printf(
                    "[PAGING STRESS] task %u "
                    "wrong mapping "
                    "VA=0x%08x expected=0x%08x "
                    "actual=0x%08x\n",
                    (unsigned)(task_index + 1),
                    (unsigned)virtual_address,
                    (unsigned)frame,
                    (unsigned)resolved
                );

                test_failed = true;
                break;
            }

            if (!paging_is_mapped(virtual_address))
            {
                printf(
                    "[PAGING STRESS] task %u "
                    "is_mapped false VA=0x%08x\n",
                    (unsigned)(task_index + 1),
                    (unsigned)virtual_address
                );

                test_failed = true;
                break;
            }

            /*
             * Force another task to get a chance.
             */
            task_yield();
        }

        /*
         * -------------------------
         * UNMAP PHASE
         * -------------------------
         */
        for (size_t i = 0;
             i < PAGING_STRESS_PAGES;
             ++i)
        {
            uintptr_t virtual_address =
                worker_base +
                i * PAGE_SIZE;

            if (frames[i] == 0)
                continue;

            /*
             * release_frame=true means paging_unmap_page()
             * also returns the physical frame to the PMM.
             */
            bool unmapped =
                paging_unmap_page(
                    virtual_address,
                    true
                );

            if (!unmapped)
            {
                printf(
                    "[PAGING STRESS] task %u "
                    "unmap failed VA=0x%08x\n",
                    (unsigned)(task_index + 1),
                    (unsigned)virtual_address
                );

                test_failed = true;
                continue;
            }

            frames[i] = 0;

            /*
             * Mapping should now be gone.
             */
            if (paging_is_mapped(virtual_address))
            {
                printf(
                    "[PAGING STRESS] task %u "
                    "mapping survived unmap "
                    "VA=0x%08x\n",
                    (unsigned)(task_index + 1),
                    (unsigned)virtual_address
                );

                test_failed = true;
            }

            task_yield();
        }

        /*
         * Emergency cleanup if something failed halfway through
         * an iteration.
         */
        if (test_failed)
        {
            for (size_t i = 0;
                 i < PAGING_STRESS_PAGES;
                 ++i)
            {
                if (frames[i] == 0)
                    continue;

                uintptr_t virtual_address =
                    worker_base +
                    i * PAGE_SIZE;

                if (paging_is_mapped(virtual_address))
                {
                    paging_unmap_page(
                        virtual_address,
                        true
                    );
                }
                else
                {
                    pmm_free_frame(frames[i]);
                }

                frames[i] = 0;
            }

            break;
        }
    }

    printf(
        "[PAGING STRESS] task %u finished\n",
        (unsigned)(task_index + 1)
    );

    increment_counter(&finished_tasks);

    task_exit();
}

void paging_stress_test(void)
{
    printf("\n[PAGING STRESS] starting\n");

    ready_tasks = 0;
    finished_tasks = 0;
    start_test = false;
    test_failed = false;

    /*
     * Create tasks first so their stacks don't affect our PMM
     * baseline.
     */
    for (size_t i = 0;
         i < PAGING_STRESS_TASKS;
         ++i)
    {
        task_t *task =
            task_create_kernel(
                paging_stress_worker,
                (void *)(uintptr_t)i
            );

        if (task == NULL)
        {
            printf(
                "[PAGING STRESS] failed to create task %u\n",
                (unsigned)(i + 1)
            );

            test_failed = true;
            return;
        }
    }

    while (ready_tasks < PAGING_STRESS_TASKS)
        task_yield();

    size_t before =
        pmm_get_free_frame_count();

    printf("[PAGING STRESS] workers ready\n");

    printf(
        "[PAGING STRESS] free frames before: %u\n",
        (unsigned)before
    );

    start_test = true;

    while (finished_tasks < PAGING_STRESS_TASKS)
        task_yield();

    size_t after =
        pmm_get_free_frame_count();

    printf(
        "[PAGING STRESS] free frames after:  %u\n",
        (unsigned)after
    );

    /*
     * IMPORTANT:
     *
     * Your paging implementation deliberately does NOT free empty
     * page tables.
     *
     * Each worker uses one separate 4 MiB PDE region, so this test
     * creates up to four new page-table frames that remain allocated.
     */
    size_t expected_page_tables =
        PAGING_STRESS_TASKS;

    if (before < after ||
        before - after != expected_page_tables)
    {
        printf(
            "[PAGING STRESS] FAIL: unexpected PMM delta "
            "(before=%u after=%u expected=%u)\n",
            (unsigned)before,
            (unsigned)after,
            (unsigned)expected_page_tables
        );

        test_failed = true;
    }

    if (test_failed)
        printf("[PAGING STRESS] FAIL\n");
    else
        printf("[PAGING STRESS] PASS\n");
}