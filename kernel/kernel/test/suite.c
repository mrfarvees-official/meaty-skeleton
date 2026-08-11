#include <stdbool.h>
#include <stdio.h>

#include <kernel/task.h>
#include <kernel/test.h>

static bool suite_started;

static void test_suite_worker(void *argument)
{
    (void)argument;

    printf("\n[TEST SUITE] automated tests started\n");

    kernel_tests_run();
    pmm_stress_test();
    paging_stress_test();
    heap_stress_test();
    mutex_stress_test();
    semaphore_stress_test();
    task_cleanup_stress_test();
    cpu_detection_test();
    smp_multitasking_stress_test();

    printf("\n[TEST SUITE] keyboard tests begin (interactive, one at a time)\n");
    keyboard_raw_test();
    keyboard_event_test();
    keyboard_blocking_test();
    keyboard_line_test();

    test_log_pass("test suite complete");
}

void kernel_tests_run_async(void)
{
    if (suite_started)
        return;

    suite_started = true;

    if (task_create_kernel(test_suite_worker, NULL) == NULL)
    {
        suite_started = false;
        test_log_fail("unable to start test suite");
    }
}
