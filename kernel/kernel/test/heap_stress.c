#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kernel/test.h>
#include <kernel/heap.h>
#include <kernel/pmm.h>
#include <kernel/task.h>

#include "../arch/i386/interrupts.h"


#define HEAP_STRESS_TASKS       40u
#define HEAP_STRESS_ITERATIONS  5000u
#define HEAP_STRESS_SLOTS       16u


static volatile size_t ready_tasks = 0;
static volatile size_t finished_tasks = 0;

static volatile bool start_test = false;
static volatile bool test_failed = false;


/*
 * Each worker publishes its live allocations here.
 *
 * This lets the controller/workers verify that two simultaneously
 * allocated blocks do not overlap.
 */
typedef struct
{
    volatile uintptr_t address;
    volatile size_t size;
} heap_test_allocation_t;


static heap_test_allocation_t
    live_allocations
        [HEAP_STRESS_TASKS]
        [HEAP_STRESS_SLOTS];


static void increment_counter(
    volatile size_t *counter)
{
    uint32_t flags =
        interrupt_save_disable();

    ++(*counter);

    interrupt_restore(flags);
}


/*
 * Check all currently published allocations for overlap.
 *
 * This is test synchronization only.
 *
 * On the current single-core kernel, interrupts are disabled so
 * another worker cannot alter the publication table midway through
 * the scan.
 */
static bool heap_check_for_overlap(void)
{
    uint32_t flags =
        interrupt_save_disable();

    for (size_t task_a = 0;
         task_a < HEAP_STRESS_TASKS;
         ++task_a)
    {
        for (size_t slot_a = 0;
             slot_a < HEAP_STRESS_SLOTS;
             ++slot_a)
        {
            uintptr_t address_a =
                live_allocations
                    [task_a]
                    [slot_a]
                    .address;

            size_t size_a =
                live_allocations
                    [task_a]
                    [slot_a]
                    .size;

            if (address_a == 0 ||
                size_a == 0)
            {
                continue;
            }

            uintptr_t end_a =
                address_a +
                size_a;

            for (size_t task_b = task_a;
                 task_b < HEAP_STRESS_TASKS;
                 ++task_b)
            {
                size_t first_slot = 0;

                if (task_b == task_a)
                    first_slot = slot_a + 1;

                for (size_t slot_b = first_slot;
                     slot_b < HEAP_STRESS_SLOTS;
                     ++slot_b)
                {
                    uintptr_t address_b =
                        live_allocations
                            [task_b]
                            [slot_b]
                            .address;

                    size_t size_b =
                        live_allocations
                            [task_b]
                            [slot_b]
                            .size;

                    if (address_b == 0 ||
                        size_b == 0)
                    {
                        continue;
                    }

                    uintptr_t end_b =
                        address_b +
                        size_b;

                    bool overlaps =
                        address_a < end_b &&
                        address_b < end_a;

                    if (overlaps)
                    {
                        printf(
                            "[HEAP STRESS] OVERLAP\n"
                        );

                        printf(
                            "  task %u slot %u: "
                            "0x%08x size=%u\n",
                            (unsigned)(task_a + 1),
                            (unsigned)slot_a,
                            (unsigned)address_a,
                            (unsigned)size_a
                        );

                        printf(
                            "  task %u slot %u: "
                            "0x%08x size=%u\n",
                            (unsigned)(task_b + 1),
                            (unsigned)slot_b,
                            (unsigned)address_b,
                            (unsigned)size_b
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


static void heap_stress_worker(
    void *argument)
{
    size_t task_index =
        (size_t)(uintptr_t)argument;

    void *pointers[HEAP_STRESS_SLOTS];
    size_t sizes[HEAP_STRESS_SLOTS];

    for (size_t i = 0;
         i < HEAP_STRESS_SLOTS;
         ++i)
    {
        pointers[i] = NULL;
        sizes[i] = 0;

        live_allocations
            [task_index]
            [i]
            .address = 0;

        live_allocations
            [task_index]
            [i]
            .size = 0;
    }


    increment_counter(
        &ready_tasks
    );


    while (!start_test)
        task_yield();


    for (size_t iteration = 0;
         iteration < HEAP_STRESS_ITERATIONS;
         ++iteration)
    {
        /*
         * ----------------------------------------------------------
         * ALLOCATION PHASE
         * ----------------------------------------------------------
         */
        for (size_t i = 0;
             i < HEAP_STRESS_SLOTS;
             ++i)
        {
            /*
             * Generate varying sizes without needing a random-number
             * generator.
             *
             * Range is approximately 16 .. 2048 bytes.
             */
            size_t requested_size =
                16u +
                (
                    (
                        iteration * 37u +
                        i * 101u +
                        task_index * 53u
                    )
                    %
                    2033u
                );

            void *pointer =
                kmalloc(
                    requested_size
                );

            if (pointer == NULL)
            {
                printf(
                    "[HEAP STRESS] task %u "
                    "kmalloc failed "
                    "iteration=%u slot=%u "
                    "size=%u\n",
                    (unsigned)(task_index + 1),
                    (unsigned)iteration,
                    (unsigned)i,
                    (unsigned)requested_size
                );

                test_failed = true;
                break;
            }

            /*
             * Heap alignment requirement.
             */
            if (((uintptr_t)pointer &
                 0xFu) != 0)
            {
                printf(
                    "[HEAP STRESS] task %u "
                    "unaligned allocation "
                    "0x%08x\n",
                    (unsigned)(task_index + 1),
                    (unsigned)(uintptr_t)pointer
                );

                test_failed = true;
            }

            pointers[i] = pointer;
            sizes[i] = requested_size;

            /*
             * Fill the allocation with a task/slot-specific byte.
             */
            uint8_t pattern =
                (uint8_t)(
                    0x20u +
                    task_index * 17u +
                    i
                );

            memset(
                pointer,
                pattern,
                requested_size
            );

            /*
             * Publish only after allocation and initialization.
             */
            uint32_t flags =
                interrupt_save_disable();

            live_allocations
                [task_index]
                [i]
                .size =
                requested_size;

            live_allocations
                [task_index]
                [i]
                .address =
                (uintptr_t)pointer;

            interrupt_restore(flags);

            task_yield();
        }


        if (test_failed)
            break;


        /*
         * Periodically check that no simultaneously-live allocations
         * overlap.
         */
        if ((iteration % 50u) == 0)
        {
            if (!heap_check_for_overlap())
            {
                test_failed = true;
                break;
            }
        }


        /*
         * ----------------------------------------------------------
         * CONTENT VERIFICATION PHASE
         * ----------------------------------------------------------
         */
        for (size_t i = 0;
             i < HEAP_STRESS_SLOTS;
             ++i)
        {
            if (pointers[i] == NULL)
                continue;

            uint8_t pattern =
                (uint8_t)(
                    0x20u +
                    task_index * 17u +
                    i
                );

            uint8_t *bytes =
                (uint8_t *)pointers[i];

            for (size_t byte = 0;
                 byte < sizes[i];
                 ++byte)
            {
                if (bytes[byte] != pattern)
                {
                    printf(
                        "[HEAP STRESS] task %u "
                        "memory corruption "
                        "iteration=%u "
                        "slot=%u byte=%u\n",
                        (unsigned)(task_index + 1),
                        (unsigned)iteration,
                        (unsigned)i,
                        (unsigned)byte
                    );

                    test_failed = true;
                    break;
                }
            }

            if (test_failed)
                break;
        }


        if (test_failed)
            break;


        /*
         * ----------------------------------------------------------
         * REALLOC PHASE
         * ----------------------------------------------------------
         *
         * Reallocate every fourth slot.
         *
         * This specifically exercises the recursive-lock hazard that
         * we removed from krealloc().
         */
        for (size_t i = 0;
             i < HEAP_STRESS_SLOTS;
             i += 4)
        {
            if (pointers[i] == NULL)
                continue;

            size_t old_size =
                sizes[i];

            size_t new_size =
                old_size + 128u;

            uint8_t pattern =
                (uint8_t)(
                    0x20u +
                    task_index * 17u +
                    i
                );

            /*
             * Temporarily stop publishing the old allocation while
             * realloc may move it.
             */
            uint32_t flags =
                interrupt_save_disable();

            live_allocations
                [task_index]
                [i]
                .address = 0;

            live_allocations
                [task_index]
                [i]
                .size = 0;

            interrupt_restore(flags);


            void *replacement =
                krealloc(
                    pointers[i],
                    new_size
                );

            if (replacement == NULL)
            {
                printf(
                    "[HEAP STRESS] task %u "
                    "krealloc failed "
                    "iteration=%u slot=%u\n",
                    (unsigned)(task_index + 1),
                    (unsigned)iteration,
                    (unsigned)i
                );

                test_failed = true;
                break;
            }

            /*
             * Existing bytes must survive realloc.
             */
            uint8_t *bytes =
                (uint8_t *)replacement;

            for (size_t byte = 0;
                 byte < old_size;
                 ++byte)
            {
                if (bytes[byte] != pattern)
                {
                    printf(
                        "[HEAP STRESS] task %u "
                        "realloc corrupted data "
                        "iteration=%u "
                        "slot=%u byte=%u\n",
                        (unsigned)(task_index + 1),
                        (unsigned)iteration,
                        (unsigned)i,
                        (unsigned)byte
                    );

                    test_failed = true;
                    break;
                }
            }

            if (test_failed)
                break;

            /*
             * Fill the enlarged allocation.
             */
            memset(
                replacement,
                pattern,
                new_size
            );

            pointers[i] =
                replacement;

            sizes[i] =
                new_size;

            flags =
                interrupt_save_disable();

            live_allocations
                [task_index]
                [i]
                .size =
                new_size;

            live_allocations
                [task_index]
                [i]
                .address =
                (uintptr_t)replacement;

            interrupt_restore(flags);

            task_yield();
        }


        if (test_failed)
            break;


        /*
         * ----------------------------------------------------------
         * FREE PHASE
         * ----------------------------------------------------------
         *
         * Free even entries first, then odd entries.
         *
         * This creates fragmentation and exercises block merging.
         */


        for (size_t i = 0;
             i < HEAP_STRESS_SLOTS;
             i += 2)
        {
            if (pointers[i] == NULL)
                continue;

            uint32_t flags =
                interrupt_save_disable();

            live_allocations
                [task_index]
                [i]
                .address = 0;

            live_allocations
                [task_index]
                [i]
                .size = 0;

            interrupt_restore(flags);


            kfree(
                pointers[i]
            );

            pointers[i] = NULL;
            sizes[i] = 0;

            task_yield();
        }


        for (size_t i = 1;
             i < HEAP_STRESS_SLOTS;
             i += 2)
        {
            if (pointers[i] == NULL)
                continue;

            uint32_t flags =
                interrupt_save_disable();

            live_allocations
                [task_index]
                [i]
                .address = 0;

            live_allocations
                [task_index]
                [i]
                .size = 0;

            interrupt_restore(flags);


            kfree(
                pointers[i]
            );

            pointers[i] = NULL;
            sizes[i] = 0;

            task_yield();
        }
    }


    /*
     * --------------------------------------------------------------
     * EMERGENCY CLEANUP
     * --------------------------------------------------------------
     */
    for (size_t i = 0;
         i < HEAP_STRESS_SLOTS;
         ++i)
    {
        if (pointers[i] == NULL)
            continue;

        uint32_t flags =
            interrupt_save_disable();

        live_allocations
            [task_index]
            [i]
            .address = 0;

        live_allocations
            [task_index]
            [i]
            .size = 0;

        interrupt_restore(flags);

        kfree(
            pointers[i]
        );

        pointers[i] = NULL;
        sizes[i] = 0;
    }


    printf(
        "[HEAP STRESS] task %u finished\n",
        (unsigned)(task_index + 1)
    );

    increment_counter(
        &finished_tasks
    );

    task_exit();
}


void heap_stress_test(void)
{
    printf(
        "\n[HEAP STRESS] starting\n"
    );

    ready_tasks = 0;
    finished_tasks = 0;

    start_test = false;
    test_failed = false;


    /*
     * Reset publication table.
     */
    for (size_t task = 0;
         task < HEAP_STRESS_TASKS;
         ++task)
    {
        for (size_t slot = 0;
             slot < HEAP_STRESS_SLOTS;
             ++slot)
        {
            live_allocations
                [task]
                [slot]
                .address = 0;

            live_allocations
                [task]
                [slot]
                .size = 0;
        }
    }


    /*
     * Create workers before taking the PMM baseline.
     *
     * task_create_kernel() itself allocates task structures and
     * stacks through the heap.
     */
    for (size_t i = 0;
         i < HEAP_STRESS_TASKS;
         ++i)
    {
        task_t *task =
            task_create_kernel(
                heap_stress_worker,
                (void *)(uintptr_t)i
            );

        if (task == NULL)
        {
            printf(
                "[HEAP STRESS] failed to create "
                "task %u\n",
                (unsigned)(i + 1)
            );

            test_failed = true;
            return;
        }
    }


    while (ready_tasks <
           HEAP_STRESS_TASKS)
    {
        task_yield();
    }


    size_t frames_before =
        pmm_get_free_frame_count();

    printf(
        "[HEAP STRESS] workers ready\n"
    );

    printf(
        "[HEAP STRESS] free frames before: %u\n",
        (unsigned)frames_before
    );


    start_test = true;


    while (finished_tasks <
           HEAP_STRESS_TASKS)
    {
        task_yield();
    }


    size_t frames_after =
        pmm_get_free_frame_count();

    printf(
        "[HEAP STRESS] free frames after:  %u\n",
        (unsigned)frames_after
    );


    /*
     * IMPORTANT:
     *
     * Your current heap expands but does NOT return completely unused
     * heap pages back to paging/PMM.
     *
     * Therefore frames_after may legitimately be lower than
     * frames_before.
     *
     * We do NOT require equality here.
     *
     * The important conditions for this test are:
     *
     *     - no deadlock
     *     - no crash/page fault
     *     - no overlapping live allocations
     *     - no corrupted allocation contents
     *     - realloc preserves old contents
     */
    if (test_failed)
    {
        printf(
            "[HEAP STRESS] FAIL\n"
        );
    }
    else
    {
        printf(
            "[HEAP STRESS] PASS\n"
        );
    }
}