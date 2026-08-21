#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/address_space.h>
#include <kernel/heap.h>
#include <kernel/logger.h>
#include <kernel/paging.h>
#include <kernel/task.h>
#include <kernel/user_thread.h>

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
     * Copy launch information before freeing the temporary
     * kernel object.
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

    task_t *task =
        task_current();

    if (task == NULL ||
        task->address_space == NULL)
    {
        log_error(
            "U12.4: worker has no task/address space\n");

        task_exit();
    }

    if (address_space_is_kernel(
            task->address_space))
    {
        log_error(
            "U12.4: worker unexpectedly has kernel address space\n");

        task_exit();
    }

    uintptr_t expected_directory =
        address_space_page_directory(
            task->address_space);

    uintptr_t current_directory =
        paging_current_directory();

    /*
     * This is the critical threading invariant:
     *
     * the worker must enter ring 3 in exactly the same
     * address space retained by its task_t.
     */
    if (expected_directory == 0 ||
        current_directory !=
            expected_directory ||
        current_directory ==
            paging_kernel_directory())
    {
        log_error(
            "U12.4: worker CR3 mismatch "
            "tid=%u current=0x%lx expected=0x%lx\n",
            (unsigned)task->id,
            (unsigned long)current_directory,
            (unsigned long)expected_directory);

        task_exit();
    }

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
     * stack_pointer represents function-entry ESP.
     *
     * stack_top - 4 contains a zero dummy return address because
     * U12.3B zeroed every physical stack page.
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
            "slot=%lu esp=0x%lx range=[0x%lx,0x%lx)\n",
            (unsigned long)stack_slot,
            (unsigned long)stack_pointer,
            (unsigned long)stack_bottom,
            (unsigned long)stack_top);

        task_exit();
    }

    log_success(
        "U12.4: worker entering userspace "
        "tid=%u CR3=0x%lx slot=%lu ESP=0x%lx\n",
        (unsigned)task->id,
        (unsigned long)current_directory,
        (unsigned long)stack_slot,
        (unsigned long)stack_pointer);

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
    task_t *parent =
        task_current();

    if (parent == NULL ||
        parent->address_space == NULL)
    {
        return 0;
    }

    address_space_t *space =
        parent->address_space;

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
     * This syscall originates from ring 3.
     *
     * Therefore the caller's userspace CR3 must currently
     * be installed.
     */
    if (paging_current_directory() !=
        user_directory)
    {
        return 0;
    }

    /*
     * Validate that entry belongs to a present PAGE_USER mapping
     * while the caller's user directory is still active.
     *
     * i386 without NX cannot distinguish executable mappings,
     * but PAGE_PRESENT | PAGE_USER at least proves that this is
     * memory belonging to the process.
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

    task_id_t parent_tid =
        parent->id;

    /*
     * Stack construction and task_create_user_unpublished()
     * currently require canonical kernel CR3.
     *
     * Disable local interrupts before temporarily changing CR3.
     * This CPU must not be scheduled while its current task still
     * logically represents the parent userspace task.
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
     * Allocate launch metadata before creating the physical stack.
     *
     * If this fails, nothing has been changed in the user's
     * address space.
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

    address_space_user_stack_t stack;

    /*
     * U12.3B:
     *
     *     reserve slot
     *     allocate 256 frames
     *     zero frames
     *     map PAGE_USER | PAGE_WRITABLE
     *     leave guard page absent
     */
    if (!address_space_user_stack_create(
            space,
            &stack))
    {
        kfree(
            launch);

        /*
         * A partially-created stack cannot currently be rolled
         * back transactionally.
         *
         * Treat this as a bring-up invariant failure rather than
         * returning to userspace with a poisoned stack slot.
         */
        log_error(
            "U12.4: worker physical stack creation failed\n");

        user_thread_halt();
    }

    if (stack.mapped_page_count !=
        ADDRESS_SPACE_USER_STACK_SIZE /
            PAGE_SIZE)
    {
        log_error(
            "U12.4: incomplete worker stack mapping\n");

        user_thread_halt();
    }

    /*
     * Enter the C worker as if it had been called.
     *
     * The word at stack_top - 4 is zero because the entire
     * U12.3B stack was zero-filled.
     *
     * The worker must call SYS_EXIT and must never return.
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
     * launch was allocated after this userspace address space
     * originally came into existence.
     *
     * Explicitly expose the supervisor-only kernel PDE containing
     * the launch object to the user's page directory.
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
            "U12.4: failed sharing launch metadata PDE\n");

        user_thread_halt();
    }

    /*
     * The new task retains its own independent reference to the
     * SAME address_space_t.
     */
    task_t *worker =
        task_create_user_unpublished(
            user_thread_kernel_entry,
            launch,
            space,
            SCHED_POLICY_REALTIME);

    if (worker == NULL)
    {
        /*
         * The stack is already physically installed.
         *
         * Until U12.5 has transactional stack destruction,
         * this failure is fatal rather than leaking a partially
         * owned worker stack back into a live process.
         */
        log_error(
            "U12.4: failed allocating worker task\n");

        user_thread_halt();
    }

    task_id_t worker_tid =
        worker->id;

    uintptr_t worker_directory =
        address_space_page_directory(
            worker->address_space);

    /*
     * Before publication we can safely inspect worker.
     */
    if (worker->address_space !=
            space ||
        worker_directory !=
            user_directory)
    {
        log_error(
            "U12.4: worker does not share parent address space\n");

        user_thread_halt();
    }

    log_success(
        "U12.4: thread prepared "
        "parent=%u worker=%u CR3=0x%lx "
        "slot=%lu stack=[0x%lx,0x%lx)\n",
        (unsigned)parent_tid,
        (unsigned)worker_tid,
        (unsigned long)worker_directory,
        (unsigned long)stack.slot_index,
        (unsigned long)stack.stack_bottom,
        (unsigned long)stack.stack_top);

    /*
     * From this point another CPU may immediately execute worker.
     */
    task_publish(
        worker);

    /*
     * Restore the parent userspace CR3 before restoring IF and
     * returning through the syscall interrupt frame.
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