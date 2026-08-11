#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <kernel/test.h>
#include <kernel/heap.h>
#include <kernel/pmm.h>
#include <kernel/task.h>
#include <kernel/scheduler.h>
#include <kernel/timer.h>
#include <kernel/tty.h>
#include <kernel/wait_queue.h>
#include <kernel/sleep_queue.h>

/*
 * ============================================================
 * Common test helpers
 * ============================================================
 */

static void test_halt(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

void test_log_pass(const char *name)
{
    terminal_setcolor(0x0Au);
    printf("[PASS]");
    terminal_setcolor(0x07u);
    printf(" %s\n", name);
}

void test_log_fail(const char *name)
{
    terminal_setcolor(0x0Cu);
    printf("[FAIL ]");
    terminal_setcolor(0x07u);
    printf(" %s\n", name);
}


/*
 * ============================================================
 * PRINTF TEST
 * ============================================================
 */

static void test_printf(void)
{
    printf("[TEST] printf\n");

    printf("String: %s\n", "kernel");
    printf("Character: %c\n", 'A');
    printf("Signed: %d\n", -123);
    printf("Unsigned: %u\n", 123U);
    printf("Octal: %o\n", 10U);
    printf("Hex: %x\n", 255U);
    printf("HEX: %X\n", 255U);
    printf("Pointer: %p\n", (void*)0x1234);
    printf("Width: |%10d|\n", 42);
    printf("Left: |%-10d|\n", 42);
    printf("Zero: |%010d|\n", -42);
    printf("Prefix: %#x\n", 255U);
    printf("Percent: 100%%\n");
    printf("Limited string: %.3s\n", "Hello");

    printf("zero:       %Lf\n", 0.0L);
    printf("negative:   %Lf\n", -123.456L);
    printf("small e:    %Le\n", 0.000012345L);
    printf("large e:    %Le\n", 123456789.0L);
    printf("g fixed:    %.5Lg\n", 12.34567L);
    printf("g exponent: %.5Lg\n", 1234567.0L);
    printf("hex:        %La\n", 12.375L);
    printf("uppercase:  %LA\n", 12.375L);
    printf("width:      |%20.4Lf|\n", 12.375L);
    printf("left:       |%-20.4Lf|\n", 12.375L);
    printf("zero pad:   |%020.4Lf|\n", -12.375L);
    printf("sign:       |%+.4Lf|\n", 12.375L);
    printf("alternate:  |%#.0Lf|\n", 12.0L);

    test_log_pass("printf");
}


/*
 * ============================================================
 * PMM TEST
 * ============================================================
 */

static void test_pmm(void)
{
    printf("[TEST] physical memory manager\n");

    uintptr_t a = pmm_allocate_frame();
    uintptr_t b = pmm_allocate_frame();

    printf("frame a = 0x%x\n", (unsigned)a);
    printf("frame b = 0x%x\n", (unsigned)b);

    if (a == 0 || b == 0 || a == b)
    {
        printf("[FAIL] PMM allocation\n");
        test_halt();
    }

    pmm_free_frame(a);

    uintptr_t c = pmm_allocate_frame();

    printf("frame c = 0x%x\n", (unsigned)c);

    if (c == 0)
    {
        printf("[FAIL] PMM reallocation\n");
        test_halt();
    }

    pmm_free_frame(b);
    pmm_free_frame(c);

    test_log_pass("physical memory manager");
}


/*
 * ============================================================
 * HEAP TEST
 * ============================================================
 */

static void test_heap(void)
{
    printf("[TEST] heap\n");

    void* first  = kmalloc(32);
    void* second = kmalloc(64);
    void* third  = kcalloc(16, sizeof(uint32_t));

    printf("first  = %p\n", first);
    printf("second = %p\n", second);
    printf("third  = %p\n", third);

    if (first == NULL ||
        second == NULL ||
        third == NULL)
    {
        printf("[FAIL] heap initial allocation\n");
        test_halt();
    }

    uint32_t* values = (uint32_t*)third;

    for (size_t i = 0; i < 16; ++i)
    {
        if (values[i] != 0)
        {
            printf("[FAIL] calloc did not zero memory\n");
            test_halt();
        }
    }

    kfree(second);

    void* reused = kmalloc(48);

    printf("reused = %p\n", reused);

    if (reused == NULL)
    {
        printf("[FAIL] heap reuse allocation\n");
        test_halt();
    }

    void* resized = krealloc(first, 256);

    printf("resized = %p\n", resized);

    if (resized == NULL)
    {
        printf("[FAIL] krealloc\n");
        test_halt();
    }

    first = resized;

    kfree(first);
    kfree(third);
    kfree(reused);

    test_log_pass("heap");
}


/*
 * ============================================================
 * HEAP EXPANSION TEST
 * ============================================================
 */

static void test_heap_expansion(void)
{
    printf("[TEST] heap expansion\n");

    void* allocations[512];

    for (size_t i = 0; i < 512; ++i)
    {
        allocations[i] = kmalloc(128);

        if (allocations[i] == NULL)
        {
            printf(
                "[FAIL] heap expansion at allocation %u\n",
                (unsigned)i
            );

            test_halt();
        }
    }

    for (size_t i = 0; i < 512; ++i)
        kfree(allocations[i]);

    test_log_pass("heap expansion");
}


/*
 * ============================================================
 * MEMORY STATISTICS TEST
 * ============================================================
 */

static void test_memory_statistics(void)
{
    printf("[TEST] memory statistics\n");

    printf(
        "usable RAM: %u MiB\n",
        (unsigned)(
            pmm_get_usable_memory() /
            (1024u * 1024u)
        )
    );

    printf(
        "free RAM: %u MiB\n",
        (unsigned)(
            pmm_get_free_memory() /
            (1024u * 1024u)
        )
    );

    test_log_pass("memory statistics");
}


/*
 * ============================================================
 * SCHEDULER TEST
 * ============================================================
 */

static void test_worker(void* argument)
{
    unsigned id = (unsigned)(uintptr_t)argument;

    for (unsigned i = 0; i < 4; ++i)
    {
        printf(
            "task %u iteration %u\n",
            id,
            i
        );

        task_yield();
    }
}


static void test_scheduler(void)
{
    printf("[TEST] scheduler/tasks\n");

    for (unsigned i = 1; i <= 4; ++i)
    {
        task_t* task =
            task_create_kernel(
                test_worker,
                (void*)(uintptr_t)i
            );

        if (task == NULL)
        {
            printf(
                "[FAIL] unable to create task %u\n",
                i
            );

            test_halt();
        }

        printf(
            "created task %u: task=%p stack=%p\n",
            i,
            task,
            (void*)task->stack_pointer
        );
    }

    /*
     * Allow the created tasks to begin running.
     */
    task_yield();

    test_log_pass("scheduler/tasks");
}


/*
 * ============================================================
 * BLOCKING / WAKEUP TEST
 * ============================================================
 */


/*
 * Test queues.
 */

static wait_queue_t single_wait_queue;
static wait_queue_t multiple_wait_queue;


/*
 * Test state.
 */

static volatile bool single_task_blocked = false;
static volatile bool single_task_woke = false;

static volatile unsigned multi_blocked_count = 0;
static volatile unsigned multi_woke_count = 0;


/*
 * Blocking-test failure helper.
 *
 * Unlike test_halt(), interrupts remain enabled so that scheduler
 * and timer state remain observable while debugging.
 */

static void blocking_test_fail(const char* message)
{
    printf(
        "[BLOCK TEST] FAILED: %s\n",
        message
    );

    for (;;)
        __asm__ volatile ("hlt");
}


/*
 * ------------------------------------------------------------
 * Blocking test 1
 *
 * One task blocks.
 * Another task wakes it.
 * ------------------------------------------------------------
 */

static void single_blocking_task(void* argument)
{
    (void)argument;

    printf("[BLOCK TEST 1] blocker: running\n");

    single_task_blocked = true;

    printf("[BLOCK TEST 1] blocker: blocking now\n");

    wait_queue_block(&single_wait_queue);

    /*
     * Execution must only resume after wake_one().
     */
    single_task_woke = true;

    printf(
        "[BLOCK TEST 1] blocker: WOKE successfully\n"
    );

    task_exit();
}


static void single_waking_task(void* argument)
{
    (void)argument;

    /*
     * Wait until the blocker reaches the blocking path.
     */
    while (!single_task_blocked)
        task_yield();

    /*
     * Give the blocker enough scheduling opportunities to actually
     * enter wait_queue_block().
     */
    for (unsigned i = 0; i < 5; ++i)
        task_yield();

    size_t count =
        wait_queue_count(
            &single_wait_queue
        );

    printf(
        "[BLOCK TEST 1] queue count before wake = %u\n",
        (unsigned)count
    );

    if (single_task_woke)
    {
        blocking_test_fail(
            "blocked task continued before wake"
        );
    }

    if (count != 1u)
    {
        blocking_test_fail(
            "expected exactly one blocked task"
        );
    }

    printf(
        "[BLOCK TEST 1] waker: waking one\n"
    );

    task_t* task =
        wait_queue_wake_one(
            &single_wait_queue
        );

    if (task == NULL)
    {
        blocking_test_fail(
            "wait_queue_wake_one returned NULL"
        );
    }

    if (wait_queue_count(&single_wait_queue) != 0u)
    {
        blocking_test_fail(
            "queue was not empty after wake_one"
        );
    }

    /*
     * Let the awakened task run.
     */
    for (unsigned i = 0; i < 10; ++i)
        task_yield();

    if (!single_task_woke)
    {
        blocking_test_fail(
            "woken task never resumed"
        );
    }

    printf(
        "[BLOCK TEST 1] PASS\n"
    );

    task_exit();
}


/*
 * ------------------------------------------------------------
 * Blocking test 2
 *
 * Three tasks block on the same queue.
 * Another task wakes all of them.
 * ------------------------------------------------------------
 */

static void multiple_blocking_task(void* argument)
{
    unsigned id =
        (unsigned)(uintptr_t)argument;

    printf(
        "[BLOCK TEST 2] blocker %u: blocking\n",
        id
    );

    ++multi_blocked_count;

    wait_queue_block(
        &multiple_wait_queue
    );

    ++multi_woke_count;

    printf(
        "[BLOCK TEST 2] blocker %u: WOKE\n",
        id
    );

    task_exit();
}


static void multiple_waking_task(void* argument)
{
    (void)argument;

    /*
     * Wait until every blocker has entered its blocking path.
     */
    while (multi_blocked_count < 3u)
        task_yield();

    /*
     * Give all blockers an opportunity to actually switch out.
     */
    for (unsigned i = 0; i < 10; ++i)
        task_yield();

    size_t count =
        wait_queue_count(
            &multiple_wait_queue
        );

    printf(
        "[BLOCK TEST 2] queue contains %u tasks\n",
        (unsigned)count
    );

    if (count != 3u)
    {
        blocking_test_fail(
            "expected three tasks in wait queue"
        );
    }

    if (multi_woke_count != 0u)
    {
        blocking_test_fail(
            "a blocked task resumed without being woken"
        );
    }

    printf(
        "[BLOCK TEST 2] waking all\n"
    );

    size_t awakened =
        wait_queue_wake_all(
            &multiple_wait_queue
        );

    printf(
        "[BLOCK TEST 2] wake_all returned %u\n",
        (unsigned)awakened
    );

    if (awakened != 3u)
    {
        blocking_test_fail(
            "wake_all did not wake exactly three tasks"
        );
    }

    if (wait_queue_count(&multiple_wait_queue) != 0u)
    {
        blocking_test_fail(
            "wait queue not empty after wake_all"
        );
    }

    /*
     * Wait until all awakened blockers have resumed.
     */
    while (multi_woke_count < 3u)
        task_yield();

    printf(
        "[BLOCK TEST 2] all three tasks resumed\n"
    );

    printf(
        "[BLOCK TEST 2] PASS\n"
    );

    task_exit();
}


/*
 * Main blocking/wakeup test.
 */

static void test_blocking_wakeup(void)
{
    printf(
        "\n"
        "========================================\n"
        " BLOCKING / WAKEUP TEST\n"
        "========================================\n"
    );

    /*
     * Reset test state.
     */
    single_task_blocked = false;
    single_task_woke = false;

    multi_blocked_count = 0;
    multi_woke_count = 0;

    wait_queue_initialize(
        &single_wait_queue
    );

    wait_queue_initialize(
        &multiple_wait_queue
    );


    /*
     * --------------------------------------------------------
     * Test 1
     * --------------------------------------------------------
     */

    task_t* single_blocker =
        task_create_kernel(
            single_blocking_task,
            NULL
        );

    task_t* single_waker =
        task_create_kernel(
            single_waking_task,
            NULL
        );

    if (single_blocker == NULL ||
        single_waker == NULL)
    {
        blocking_test_fail(
            "failed to create TEST 1 tasks"
        );
    }


    /*
     * --------------------------------------------------------
     * Test 2
     * --------------------------------------------------------
     */

    task_t* blocker1 =
        task_create_kernel(
            multiple_blocking_task,
            (void*)(uintptr_t)1u
        );

    task_t* blocker2 =
        task_create_kernel(
            multiple_blocking_task,
            (void*)(uintptr_t)2u
        );

    task_t* blocker3 =
        task_create_kernel(
            multiple_blocking_task,
            (void*)(uintptr_t)3u
        );

    task_t* multi_waker =
        task_create_kernel(
            multiple_waking_task,
            NULL
        );

    if (blocker1 == NULL ||
        blocker2 == NULL ||
        blocker3 == NULL ||
        multi_waker == NULL)
    {
        blocking_test_fail(
            "failed to create TEST 2 tasks"
        );
    }

    printf(
        "[BLOCK TEST] tasks created\n"
    );

    /*
     * Start executing the created tasks.
     *
     * Timer preemption may also cause them to run.
     */
    task_yield();

    /*
     * The bootstrap task eventually returns here.
     *
     * Wait until both blocking tests have completed.
     */
    while (!single_task_woke ||
           multi_woke_count < 3u)
    {
        task_yield();
    }

    /*
     * Give the waker tasks a chance to finish their PASS paths and
     * call task_exit() as well.
     */
    for (unsigned i = 0; i < 10; ++i)
        task_yield();

    printf(
        "\n"
        "========================================\n"
        " BLOCKING / WAKEUP TEST PASSED\n"
        "========================================\n"
    );
}


/*
 * ============================================================
 * DESTRUCTIVE / NON-RETURNING TESTS
 * ============================================================
 *
 * Do not place these in the regular test table.
 * ============================================================
 */

static void test_page_fault(void)
{
    printf("[TEST] page fault\n");

    volatile uint32_t* invalid_address =
        (volatile uint32_t*)0xD0000000u;

    *invalid_address = 0xDEADBEEFu;

    /*
     * Must never reach here.
     */
    printf("[FAIL] page fault did not occur\n");

    test_halt();
}


static void test_pit(void)
{
    printf("[TEST] PIT\n");

    uint64_t previous =
        timer_ticks();

    for (;;)
    {
        uint64_t now =
            timer_ticks();

        /*
         * timer_frequency() ticks ~= one second.
         */
        if (now - previous >= timer_frequency())
        {
            previous = now;

            printf(
                "ticks=%lu uptime=%lu ms\n",
                (unsigned long)now,
                (unsigned long)timer_uptime_ms()
            );
        }

        /*
         * IF must already be enabled.
         */
        __asm__ volatile ("hlt");
    }
}

/*
 * ============================================================
 * SLEEP QUEUE TEST
 * ============================================================
 */

static volatile unsigned sleep_test_woke_count = 0;

static volatile uint64_t sleep_test_wake_time[4];


/*
 * Each worker sleeps for a different duration.
 *
 * id 1 -> 500 ms
 * id 2 -> 1000 ms
 * id 3 -> 1500 ms
 */
static void sleep_test_worker(void* argument)
{
    unsigned id =
        (unsigned)(uintptr_t)argument;

    uint64_t sleep_ms =
        (uint64_t)id * 500u;

    uint64_t before =
        timer_uptime_ms();

    printf(
        "[SLEEP TEST] task %u sleeping %lu ms at %lu ms\n",
        id,
        (unsigned long)sleep_ms,
        (unsigned long)before
    );

    task_sleep(sleep_ms);

    uint64_t after =
        timer_uptime_ms();

    sleep_test_wake_time[id] =
        after;

    ++sleep_test_woke_count;

    printf(
        "[SLEEP TEST] task %u woke at %lu ms\n",
        id,
        (unsigned long)after
    );

    /*
     * A sleeping task must not wake earlier than requested.
     */
    if (after < before + sleep_ms)
    {
        printf(
            "[SLEEP TEST] FAIL: task %u woke too early\n",
            id
        );

        test_halt();
    }

    task_exit();
}


static void test_sleep_queue(void)
{
    printf(
        "\n"
        "========================================\n"
        " SLEEP QUEUE TEST\n"
        "========================================\n"
    );

    sleep_test_woke_count = 0;

    for (unsigned i = 0; i < 4; ++i)
        sleep_test_wake_time[i] = 0;

    /*
     * Create in reverse deadline order deliberately.
     *
     * This proves the sleep queue is ordering by wake_tick,
     * rather than simply behaving as FIFO.
     *
     * Creation order:
     *
     *     1500 ms
     *     1000 ms
     *      500 ms
     *
     * Expected wake order:
     *
     *      500 ms
     *     1000 ms
     *     1500 ms
     */

    task_t* task3 =
        task_create_kernel(
            sleep_test_worker,
            (void*)(uintptr_t)3u
        );

    task_t* task2 =
        task_create_kernel(
            sleep_test_worker,
            (void*)(uintptr_t)2u
        );

    task_t* task1 =
        task_create_kernel(
            sleep_test_worker,
            (void*)(uintptr_t)1u
        );

    if (task1 == NULL ||
        task2 == NULL ||
        task3 == NULL)
    {
        printf(
            "[SLEEP TEST] FAIL: unable to create tasks\n"
        );

        test_halt();
    }

    /*
     * Start the workers.
     */
    task_yield();

    /*
     * The bootstrap task must remain runnable while the three
     * workers are sleeping.
     *
     * Preemption / cooperative yields will let the awakened
     * workers resume when their deadlines expire.
     */
    while (sleep_test_woke_count < 3u)
    {
        task_yield();
    }

    /*
     * All three have now resumed.
     *
     * Verify deadline ordering.
     */
    if (sleep_test_wake_time[1] == 0 ||
        sleep_test_wake_time[2] == 0 ||
        sleep_test_wake_time[3] == 0)
    {
        printf(
            "[SLEEP TEST] FAIL: missing wake timestamps\n"
        );

        test_halt();
    }

    if (sleep_test_wake_time[1] >
        sleep_test_wake_time[2])
    {
        printf(
            "[SLEEP TEST] FAIL: 500ms task woke after 1000ms task\n"
        );

        test_halt();
    }

    if (sleep_test_wake_time[2] >
        sleep_test_wake_time[3])
    {
        printf(
            "[SLEEP TEST] FAIL: 1000ms task woke after 1500ms task\n"
        );

        test_halt();
    }

    printf(
        "[SLEEP TEST] wake order correct:\n"
    );

    printf(
        "    500ms  -> %lu ms\n",
        (unsigned long)sleep_test_wake_time[1]
    );

    printf(
        "    1000ms -> %lu ms\n",
        (unsigned long)sleep_test_wake_time[2]
    );

    printf(
        "    1500ms -> %lu ms\n",
        (unsigned long)sleep_test_wake_time[3]
    );

    printf(
        "[SLEEP TEST] PASS\n"
    );

    printf(
        "========================================\n"
    );
}


/*
 * ============================================================
 * Test registry
 * ============================================================
 *
 * Scheduler-dependent tests belong near the end.
 *
 * Run blocking/wakeup before sleep queues because the sleep
 * subsystem builds on the same scheduler state transitions.
 * ============================================================
 */
static const kernel_test_t tests[] =
{
    { "printf",            test_printf            },
    { "PMM",               test_pmm               },
    { "heap",              test_heap              },
    { "heap expansion",    test_heap_expansion    },
    { "memory statistics", test_memory_statistics },
    { "scheduler",         test_scheduler         },

    /*
     * Scheduler-dependent integration test.
     *
     * Keep this last.
     */
    { "blocking / wakeup", test_blocking_wakeup   },
    { "sleep queue",       test_sleep_queue       },
};


/*
 * ============================================================
 * Public test entry point
 * ============================================================
 */

void kernel_tests_run(void)
{
    const size_t test_count =
        sizeof(tests) / sizeof(tests[0]);

    printf("\n");
    printf("========================================\n");
    printf("              KERNEL TESTS\n");
    printf("========================================\n");

    for (size_t i = 0; i < test_count; ++i)
    {
        printf(
            "\n[%u/%u] %s\n",
            (unsigned)(i + 1),
            (unsigned)test_count,
            tests[i].name
        );

        tests[i].function();
    }

    printf("\n");
    printf("========================================\n");
    printf("          ALL KERNEL TESTS PASSED\n");
    printf("========================================\n");


    /*
     * Destructive/non-returning tests (test_page_fault() and test_pit())
     * are intentionally excluded from the automated suite. Run either one
     * manually when validating that subsystem.
     */
}
