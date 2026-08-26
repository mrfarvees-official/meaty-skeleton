#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/address_space.h>
#include <kernel/heap.h>
#include <kernel/logger.h>
#include <kernel/paging.h>
#include <kernel/task.h>
#include <kernel/user_thread.h>
#include <kernel/process.h>

#include "../../arch/i386/interrupts.h"

/*
 * Userspace ends at 3 GiB.
 */
#define USER_THREAD_USER_LIMIT \
    ((uintptr_t)0xC0000000u)

/*
 * Low-level architecture transition.
 *
 * Does not return.
 */
extern void arch_enter_user(
    uintptr_t instruction_pointer,
    uintptr_t stack_pointer)
    __attribute__((noreturn));

/*
 * Information retained until the scheduler first executes
 * the new userspace thread.
 */
typedef struct user_thread_launch_context
{
    uintptr_t entry;
    uintptr_t stack_pointer;

    uintptr_t stack_bottom;
    uintptr_t stack_top;

    size_t stack_slot;

} user_thread_launch_context_t;

/*
 * Fatal bring-up failure.
 *
 * U12.4 cannot safely recover from a partially-created physical
 * user stack yet because inactive-directory transactional unmapping
 * has not been implemented.
 */
static void user_thread_halt(void)
    __attribute__((noreturn));

static void user_thread_halt(void)
{
    for (;;)
        __asm__ volatile(
            "cli; hlt");
}

/*
 * First kernel function executed by a newly-created userspace
 * worker thread.
 *
 * The scheduler has already:
 *
 *     - switched task_t
 *     - switched TSS kernel stack
 *     - switched CR3 to the shared userspace address space
 */
static void user_thread_kernel_entry(
    void *argument)
    __attribute__((noreturn));

static void user_thread_kernel_entry(
    void *argument)
{
    user_thread_launch_context_t *launch =
        (user_thread_launch_context_t *)argument;

    if (launch == NULL)
    {
        log_error(
            "U12.4: worker missing launch context\n");

        task_exit();
    }

    /*
     * Copy launch metadata before releasing the temporary
     * kernel allocation.
     */
    uintptr_t entry =
        launch->entry;

    uintptr_t stack_pointer =
        launch->stack_pointer;

    uintptr_t stack_bottom =
        launch->stack_bottom;

    uintptr_t stack_top =
        launch->stack_top;

    size_t stack_slot =
        launch->stack_slot;

    kfree(
        launch);

    /*
     * ----------------------------------------------------------
     * Validate current task/process ownership.
     * ----------------------------------------------------------
     */
    task_t *task =
        task_current();

    if (task == NULL ||
        task->address_space == NULL ||
        task->process == NULL)
    {
        log_error(
            "U12.4: worker missing "
            "task/process/address space\n");

        task_exit();
    }

    process_t *process =
        task->process;

    address_space_t *process_space =
        process_address_space(
            process);

    if (process_space == NULL ||
        process_space !=
            task->address_space)
    {
        log_error(
            "U12.4: worker process/address-space "
            "mismatch pid=%u tid=%u\n",
            (unsigned)process_id(
                process),
            (unsigned)task->id);

        task_exit();
    }

    if (address_space_is_kernel(
            task->address_space))
    {
        log_error(
            "U12.4: worker unexpectedly has "
            "kernel address space\n");

        task_exit();
    }

    /*
     * ----------------------------------------------------------
     * Verify CR3.
     * ----------------------------------------------------------
     */
    uintptr_t expected_directory =
        address_space_page_directory(
            task->address_space);

    uintptr_t current_directory =
        paging_current_directory();

    if (expected_directory == 0 ||
        current_directory !=
            expected_directory ||
        current_directory ==
            paging_kernel_directory())
    {
        log_error(
            "U12.4: worker CR3 mismatch "
            "pid=%u tid=%u "
            "current=0x%lx expected=0x%lx\n",
            (unsigned)process_id(
                process),
            (unsigned)task->id,
            (unsigned long)
                current_directory,
            (unsigned long)
                expected_directory);

        task_exit();
    }

    /*
     * ----------------------------------------------------------
     * Validate userspace entry point.
     * ----------------------------------------------------------
     */
    if (entry == 0 ||
        entry >=
            USER_THREAD_USER_LIMIT)
    {
        log_error(
            "U12.4: invalid worker entry 0x%lx\n",
            (unsigned long)entry);

        task_exit();
    }

    /*
     * ----------------------------------------------------------
     * Validate userspace stack.
     * ----------------------------------------------------------
     */
    if (stack_bottom == 0 ||
        stack_top <=
            stack_bottom ||
        stack_top >
            USER_THREAD_USER_LIMIT ||
        stack_pointer <
            stack_bottom ||
        stack_pointer >=
            stack_top)
    {
        log_error(
            "U12.4: invalid worker stack "
            "slot=%lu esp=0x%lx "
            "range=[0x%lx,0x%lx)\n",
            (unsigned long)
                stack_slot,
            (unsigned long)
                stack_pointer,
            (unsigned long)
                stack_bottom,
            (unsigned long)
                stack_top);

        task_exit();
    }

    /*
     * This is now a real thread inside a real process.
     */
    // log_success(
    //     "U12.4: worker entering userspace "
    //     "pid=%u tid=%u CR3=0x%lx "
    //     "slot=%lu ESP=0x%lx\n",
    //     (unsigned)process_id(
    //         process),
    //     (unsigned)task->id,
    //     (unsigned long)
    //         current_directory,
    //     (unsigned long)
    //         stack_slot,
    //     (unsigned long)
    //         stack_pointer);

    arch_enter_user(
        entry,
        stack_pointer);
}

/*
 * Create another CPL3 execution context sharing the CURRENT
 * userspace task's address space.
 */
task_id_t user_thread_create_current(
    uintptr_t entry)
{
    /*
     * ----------------------------------------------------------
     * STEP 1
     * Get current userspace task.
     * ----------------------------------------------------------
     */
    task_t *parent =
        task_current();

    if (parent == NULL ||
        parent->address_space == NULL ||
        parent->process == NULL)
    {
        log_error(
            "U12.4: parent missing "
            "task/process/address space\n");

        return 0;
    }

    address_space_t *space =
        parent->address_space;

    process_t *process =
        parent->process;

    /*
     * Parent task and parent process must describe the same
     * address space.
     */
    if (process_address_space(
            process) !=
        space)
    {
        log_error(
            "U12.4: parent process/address-space mismatch\n");

        return 0;
    }

    if (address_space_is_kernel(
            space))
    {
        return 0;
    }

    if (entry == 0 ||
        entry >=
            USER_THREAD_USER_LIMIT)
    {
        return 0;
    }

    task_id_t parent_tid =
        parent->id;

    process_id_t pid =
        process_id(
            process);

    /*
     * ----------------------------------------------------------
     * STEP 2
     * Validate address spaces.
     * ----------------------------------------------------------
     */
    uintptr_t user_directory =
        address_space_page_directory(
            space);

    uintptr_t kernel_directory =
        paging_kernel_directory();

    if (user_directory == 0 ||
        kernel_directory == 0 ||
        user_directory ==
            kernel_directory)
    {
        return 0;
    }

    /*
     * This function originates from a userspace syscall,
     * therefore the parent's userspace CR3 must currently
     * be installed.
     */
    if (paging_current_directory() !=
        user_directory)
    {
        return 0;
    }

    /*
     * ----------------------------------------------------------
     * STEP 3
     * Validate worker entry belongs to userspace.
     * ----------------------------------------------------------
     */
    uint32_t entry_flags =
        0;

    if (!paging_get_effective_flags(
            entry,
            &entry_flags))
    {
        return 0;
    }

    if ((entry_flags &
         PAGE_PRESENT) == 0 ||
        (entry_flags &
         PAGE_USER) == 0)
    {
        return 0;
    }

    /*
     * ----------------------------------------------------------
     * STEP 4
     * Temporarily switch to canonical kernel CR3.
     *
     * Interrupts must remain disabled while task_current()
     * logically still represents the userspace parent.
     * ----------------------------------------------------------
     */
    uint32_t interrupt_flags =
        interrupt_save_disable();

    if (!paging_switch_directory(
            kernel_directory))
    {
        interrupt_restore(
            interrupt_flags);

        return 0;
    }

    /*
     * ----------------------------------------------------------
     * STEP 5
     * Allocate worker launch metadata.
     * ----------------------------------------------------------
     */
    user_thread_launch_context_t *launch =
        kmalloc(
            sizeof(*launch));

    if (launch == NULL)
    {
        if (!paging_switch_directory(
                user_directory))
        {
            user_thread_halt();
        }

        interrupt_restore(
            interrupt_flags);

        return 0;
    }

    /*
     * ----------------------------------------------------------
     * STEP 6
     * Create physical userspace worker stack.
     * ----------------------------------------------------------
     */
    address_space_user_stack_t stack;

    if (!address_space_user_stack_create(
            space,
            &stack))
    {
        kfree(
            launch);

        /*
         * Until transactional stack destruction exists,
         * treat partial creation as fatal.
         */
        log_error(
            "U12.4: worker physical "
            "stack creation failed\n");

        user_thread_halt();
    }

    if (stack.mapped_page_count !=
        ADDRESS_SPACE_USER_STACK_SIZE /
            PAGE_SIZE)
    {
        log_error(
            "U12.4: incomplete worker "
            "stack mapping\n");

        user_thread_halt();
    }

    /*
     * Enter the C worker as if a function call had occurred.
     *
     * stack_top - 4 contains the zero dummy return address.
     */
    uintptr_t worker_esp =
        stack.stack_top -
        sizeof(uint32_t);

    launch->entry =
        entry;

    launch->stack_pointer =
        worker_esp;

    launch->stack_bottom =
        stack.stack_bottom;

    launch->stack_top =
        stack.stack_top;

    launch->stack_slot =
        stack.slot_index;

    /*
     * ----------------------------------------------------------
     * STEP 7
     * Make launch metadata visible in the user page directory.
     *
     * These PDEs remain supervisor-only.
     * ----------------------------------------------------------
     */
    uintptr_t launch_last =
        (uintptr_t)launch +
        sizeof(*launch) -
        1u;

    if (!paging_share_kernel_pde(
            user_directory,
            (uintptr_t)launch) ||
        !paging_share_kernel_pde(
            user_directory,
            launch_last))
    {
        log_error(
            "U12.4: failed sharing "
            "launch metadata PDE\n");

        user_thread_halt();
    }

    /*
     * ----------------------------------------------------------
     * STEP 8
     * Create unpublished worker task.
     *
     * It receives another reference to the SAME
     * address_space_t as the parent.
     * ----------------------------------------------------------
     */
    task_t *worker =
        task_create_user_unpublished(
            user_thread_kernel_entry,
            launch,
            space,
            SCHED_POLICY_REALTIME);

    if (worker == NULL)
    {
        log_error(
            "U12.4: failed allocating "
            "worker task\n");

        user_thread_halt();
    }

    /*
     * Before publication it is safe to inspect worker.
     */
    if (worker->address_space !=
            space ||
        worker->process !=
            NULL)
    {
        log_error(
            "U12.4: new worker has invalid "
            "initial ownership\n");

        user_thread_halt();
    }

    /*
     * ----------------------------------------------------------
     * STEP 9
     * Bind worker into SAME process as parent.
     *
     * This is the important P1B operation.
     *
     * Before:
     *
     *     PID 2
     *       |
     *       +-- parent TID 5
     *
     * After:
     *
     *     PID 2
     *       |
     *       +-- parent TID 5
     *       +-- worker TID 6
     * ----------------------------------------------------------
     */
    if (!task_bind_process(
            worker,
            process))
    {
        log_error(
            "U12.4: failed binding worker "
            "to pid=%u\n",
            (unsigned)pid);

        user_thread_halt();
    }

    /*
     * ----------------------------------------------------------
     * STEP 10
     * Verify final worker ownership.
     * ----------------------------------------------------------
     */
    uintptr_t worker_directory =
        address_space_page_directory(
            worker->address_space);

    if (worker->process !=
            process ||
        worker->address_space !=
            space ||
        process_address_space(
            worker->process) !=
            space ||
        worker_directory !=
            user_directory)
    {
        log_error(
            "U12.4: worker ownership "
            "verification failed\n");

        user_thread_halt();
    }

    /*
     * There must now be at least:
     *
     *     parent
     *     worker
     *
     * attached to this process.
     */
    size_t process_threads =
        process_thread_count(
            process);

    if (process_threads < 2u)
    {
        log_error(
            "U12.4: invalid process thread "
            "count=%lu\n",
            (unsigned long)
                process_threads);

        user_thread_halt();
    }

    task_id_t worker_tid =
        worker->id;

    // log_success(
    //     "U12.4: thread prepared "
    //     "pid=%u parent=%u worker=%u "
    //     "threads=%lu CR3=0x%lx "
    //     "slot=%lu stack=[0x%lx,0x%lx)\n",
    //     (unsigned)pid,
    //     (unsigned)parent_tid,
    //     (unsigned)worker_tid,
    //     (unsigned long)
    //         process_threads,
    //     (unsigned long)
    //         worker_directory,
    //     (unsigned long)
    //         stack.slot_index,
    //     (unsigned long)
    //         stack.stack_bottom,
    //     (unsigned long)
    //         stack.stack_top);

    /*
     * ----------------------------------------------------------
     * STEP 11
     * Publish worker.
     *
     * DO NOT dereference worker after this.
     * Another CPU may immediately execute it.
     * ----------------------------------------------------------
     */
    task_publish(
        worker);

    /*
     * ----------------------------------------------------------
     * STEP 12
     * Restore parent's userspace CR3.
     * ----------------------------------------------------------
     */
    if (!paging_switch_directory(
            user_directory))
    {
        user_thread_halt();
    }

    interrupt_restore(
        interrupt_flags);

    return worker_tid;
}
