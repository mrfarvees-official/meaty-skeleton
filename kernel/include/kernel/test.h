#ifndef KERNEL_TEST_H
#define KERNEL_TEST_H

typedef void (*test_function_t)(void);

typedef struct
{
    const char      * name;
    test_function_t function;
} kernel_test_t;

void kernel_tests_run(void);
void kernel_tests_run_async(void);
void test_log_pass(const char *name);
void test_log_fail(const char *name);
void pmm_stress_test(void);
void paging_stress_test(void);
void heap_stress_test(void);
void mutex_stress_test(void);
void semaphore_stress_test(void);
void task_cleanup_stress_test(void);
void keyboard_raw_test(void);
void keyboard_event_test(void);
void keyboard_blocking_test(void);
void keyboard_line_test(void);
void cpu_detection_test(void);
void smp_multitasking_stress_test(void);
void ramfs_test(void);
void ata_test(void);
void partition_test(void);
void ext2_magic_test(void);
void partition_scan_test(void);

#endif
