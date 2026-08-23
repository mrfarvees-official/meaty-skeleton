#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/spawn.h>

#include <kernel/elf.h>
#include <kernel/heap.h>
#include <kernel/paging.h>
#include <kernel/address_space.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/user_image.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <kernel/logger.h>

/**
 * -----------------------------------------------
 * USERSPACE STACK
 * -----------------------------------------------
 *
 * Userspace lives below 0xC0000000.
 *
 * Give each newly spawned ELF program a 1 MiB stack:
 *
 *      0xC0000000  <-  stack top
 *          |
 *          | 1 MiB
 *          | 256 x 4KiB pages
 *          |
 *      0xBFF00000  <-  stack bottom
 *
 * The stack grows downward.
 */
#define PROCESS_USER_STACK_SIZE (1024u * 1024u)
#define PROCESS_USER_STACK_TOP 0xC0000000u
#define PROCESS_USER_STACK_ADDRESS (PROCESS_USER_STACK_TOP - PROCESS_USER_STACK_SIZE)

/**
 * Initial implementation read the complete ELF file into a temporary
 * kernel buffer.
 *
 * Keep that allocation bounded so a damaged filesystem entry cannot
 * cause an arbitarrily kernel allocation.
 */
#define PROCESS_MAX_EXECUTABLE_SIZE (1024u * 1024u)

/**
 * Architecture function which finally performs the ring-0 -> ring-3
 * translation.
 *
 * It never returns.
 */
extern void arch_enter_user(
    uintptr_t instruction_pointer,
    uintptr_t stack_pointer)
    __attribute__((noreturn));

/**
 * Information needed when the scheduler starts the newly-created task.
 *
 * We cannot directly enter userspace inside process_spawn_user(),
 * because that function runs in the context of the caller.
 *
 * Instead:
 *
 *      process_spawn_user()
 *          |
 *          | creates task
 *          v
 *      scheduler eventually runs task
 *          |
 *          v
 *      process_user_task_entry()
 *          |
 *          v
 *      arch_enter_user()
 */
typedef struct
{
    uintptr_t entry;
    uintptr_t stack_pointer;
} process_user_launch_context_t;

/**
 * ----------------------------------------------------------------------
 * USER TASK TRAMPOLINE
 * ----------------------------------------------------------------------
 *
 * This is the first kernel-mode function executed by the newly-created
 * userspace task.
 *
 * The scheduler has already switched to the task's private page
 * directory before this function executes.
 */
static void process_user_task_entry(
    void *arguments)
    __attribute__((noreturn));

static void process_user_task_entry(
    void *argument)
{
    process_user_launch_context_t *launch =
        (process_user_launch_context_t *)
            argument;

    if (launch == NULL)
    {
        log_error(
            "spawn: missing user launch context\n");

        task_exit();
    }

    uintptr_t entry =
        launch->entry;

    uintptr_t stack_pointer =
        launch->stack_pointer;

    /*
     * Launch structure is no longer needed once values
     * have been copied locally.
     */
    kfree(
        launch);

    if (entry == 0 ||
        stack_pointer <
            PROCESS_USER_STACK_ADDRESS ||
        stack_pointer >=
            PROCESS_USER_STACK_TOP)
    {
        log_error(
            "spawn: invalid user entry/stack "
            "entry=0x%lx esp=0x%lx\n",
            (unsigned long)entry,
            (unsigned long)
                stack_pointer);

        task_exit();
    }

    task_t *task =
        task_current();

    if (task == NULL ||
        task->address_space == NULL ||
        task->process == NULL)
    {
        log_error(
            "spawn: userspace task missing "
            "process/address space\n");

        task_exit();
    }

    /*
     * A task belonging to a process must execute
     * using that process's address space.
     */
    if (process_address_space(
            task->process) !=
        task->address_space)
    {
        log_error(
            "spawn: process/address-space mismatch "
            "pid=%u tid=%u\n",
            (unsigned)process_id(
                task->process),
            (unsigned)task->id);

        task_exit();
    }

    /*
     * Userspace must never run in the immortal
     * kernel address space.
     */
    if (address_space_is_kernel(
            task->address_space))
    {
        log_error(
            "spawn: userspace task has "
            "kernel address space\n");

        task_exit();
    }

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
            "spawn: invalid user address space "
            "pid=%u tid=%u "
            "CR3=0x%lx expected=0x%lx\n",
            (unsigned)process_id(
                task->process),
            (unsigned)task->id,
            (unsigned long)
                current_directory,
            (unsigned long)
                expected_directory);

        task_exit();
    }

    log_success(
        "spawn: entering userspace "
        "pid=%u tid=%u "
        "entry=0x%lx esp=0x%lx\n",
        (unsigned)process_id(
            task->process),
        (unsigned)task->id,
        (unsigned long)entry,
        (unsigned long)
            stack_pointer);

    arch_enter_user(
        entry,
        stack_pointer);
}

/**
 * -----------------------------------------------------------------------------
 * PROCESS SPAWN
 * -----------------------------------------------------------------------------
 */
process_id_t process_spawn_user(
    const char *path,
    size_t argc,
    const char *const argv[])
{
    if (path == NULL ||
        argc == 0 ||
        argv == NULL)
    {
        log_error(
            "spawn: invalid arguments\n");

        return 0;
    }

    /*
     * ELF loading currently requires canonical kernel CR3.
     */
    uintptr_t kernel_directory =
        paging_kernel_directory();

    if (kernel_directory == 0 ||
        paging_current_directory() !=
            kernel_directory)
    {
        log_error(
            "spawn: kernel CR3 is not active\n");

        return 0;
    }

    /*
     * ----------------------------------------------------------
     * STEP 1
     * Determine parent PID.
     *
     * Kernel-launched programs currently belong to PID 1.
     *
     * Later, when a userspace process invokes spawn(), its own
     * PID automatically becomes the parent.
     * ----------------------------------------------------------
     */
    process_id_t parent_pid =
        1u;

    task_t *current =
        task_current();

    if (current != NULL &&
        current->process != NULL)
    {
        process_id_t current_pid =
            process_id(
                current->process);

        if (current_pid !=
            PROCESS_ID_INVALID)
        {
            parent_pid =
                current_pid;
        }
    }

    /*
     * ----------------------------------------------------------
     * STEP 2
     * Open executable.
     * ----------------------------------------------------------
     */
    file_t *file =
        NULL;

    if (vfs_open(
            path,
            VFS_OPEN_READ,
            &file) != 0 ||
        file == NULL)
    {
        log_error(
            "spawn: failed opening %s\n",
            path);

        return 0;
    }

    if (file->vnode == NULL ||
        file->vnode->type !=
            VNODE_REGULAR)
    {
        log_error(
            "spawn: %s is not a regular file\n",
            path);

        vfs_close(
            file);

        return 0;
    }

    /*
     * ----------------------------------------------------------
     * STEP 3
     * Validate executable size.
     * ----------------------------------------------------------
     */
    uint64_t executable_size_64 =
        file->vnode->size;

    if (executable_size_64 == 0)
    {
        log_error(
            "spawn: executable %s is empty\n",
            path);

        vfs_close(
            file);

        return 0;
    }

    if (executable_size_64 >
            (uint64_t)SIZE_MAX ||
        executable_size_64 >
            (uint64_t)PROCESS_MAX_EXECUTABLE_SIZE)
    {
        log_error(
            "spawn: executable %s has invalid "
            "size %llu bytes\n",
            path,
            (unsigned long long)
                executable_size_64);

        vfs_close(
            file);

        return 0;
    }

    size_t executable_size =
        (size_t)executable_size_64;

    /*
     * ----------------------------------------------------------
     * STEP 4
     * Read ELF into temporary kernel buffer.
     * ----------------------------------------------------------
     */
    uint8_t *executable_data =
        kmalloc(
            executable_size);

    if (executable_data == NULL)
    {
        log_error(
            "spawn: failed allocating %lu-byte "
            "ELF buffer\n",
            (unsigned long)
                executable_size);

        vfs_close(
            file);

        return 0;
    }

    size_t total_read =
        0;

    while (total_read <
           executable_size)
    {
        size_t bytes_read =
            0;

        if (vfs_read(
                file,
                executable_data +
                    total_read,
                executable_size -
                    total_read,
                &bytes_read) != 0)
        {
            log_error(
                "spawn: VFS read failed for %s "
                "at offset %lu\n",
                path,
                (unsigned long)
                    total_read);

            kfree(
                executable_data);

            vfs_close(
                file);

            return 0;
        }

        if (bytes_read == 0)
        {
            log_error(
                "spawn: unexpected EOF for %s "
                "at %lu/%lu\n",
                path,
                (unsigned long)
                    total_read,
                (unsigned long)
                    executable_size);

            kfree(
                executable_data);

            vfs_close(
                file);

            return 0;
        }

        if (bytes_read >
            executable_size -
                total_read)
        {
            log_error(
                "spawn: VFS returned invalid "
                "read length for %s\n",
                path);

            kfree(
                executable_data);

            vfs_close(
                file);

            return 0;
        }

        total_read +=
            bytes_read;
    }

    vfs_close(
        file);

    file =
        NULL;

    if (total_read !=
        executable_size)
    {
        log_error(
            "spawn: executable read length "
            "mismatch for %s\n",
            path);

        kfree(
            executable_data);

        return 0;
    }

    log_success(
        "spawn: loaded %s (%lu bytes)\n",
        path,
        (unsigned long)
            executable_size);

    /*
     * ----------------------------------------------------------
     * STEP 5
     * Load ELF image.
     * ----------------------------------------------------------
     */
    user_image_t image;

    if (!elf_load_user_image(
            &image,
            executable_data,
            executable_size,
            PROCESS_USER_STACK_ADDRESS,
            PROCESS_USER_STACK_TOP,
            argc,
            argv))
    {
        log_error(
            "spawn: ELF loader rejected %s\n",
            path);

        kfree(
            executable_data);

        return 0;
    }

    kfree(
        executable_data);

    executable_data =
        NULL;

    if (image.page_directory == 0 ||
        image.entry == 0 ||
        image.stack_top <
            PROCESS_USER_STACK_ADDRESS ||
        image.stack_top >=
            PROCESS_USER_STACK_TOP)
    {
        log_error(
            "spawn: ELF loader produced "
            "invalid image for %s\n",
            path);

        user_image_destroy(
            &image);

        return 0;
    }

    /*
     * ----------------------------------------------------------
     * STEP 6
     * Allocate launch metadata.
     * ----------------------------------------------------------
     */
    process_user_launch_context_t *launch =
        kmalloc(
            sizeof(*launch));

    if (launch == NULL)
    {
        log_error(
            "spawn: failed allocating "
            "launch context\n");

        user_image_destroy(
            &image);

        return 0;
    }

    launch->entry =
        image.entry;

    launch->stack_pointer =
        image.stack_top;

    uintptr_t user_directory =
        image.page_directory;

    /*
     * ----------------------------------------------------------
     * STEP 7
     * Detach page directory from user_image_t.
     * ----------------------------------------------------------
     */
    uintptr_t detached_directory =
        user_image_detach_directory(
            &image);

    if (detached_directory == 0 ||
        detached_directory !=
            user_directory)
    {
        log_error(
            "spawn: failed detaching "
            "user address space\n");

        kfree(
            launch);

        user_image_destroy(
            &image);

        return 0;
    }

    /*
     * ----------------------------------------------------------
     * STEP 8
     * Adopt address space.
     * ----------------------------------------------------------
     */
    address_space_t *user_space =
        address_space_adopt_user(
            detached_directory);

    if (user_space == NULL)
    {
        log_error(
            "spawn: failed adopting "
            "user address space\n");

        paging_destroy_user_directory(
            detached_directory);

        kfree(
            launch);

        return 0;
    }

    /*
     * Register ELF main stack as stack slot zero.
     */
    address_space_user_stack_slot_t
        main_stack_slot;

    if (!address_space_user_stack_slot_reserve_index(
            user_space,
            0u,
            &main_stack_slot))
    {
        log_error(
            "spawn: failed reserving "
            "ELF main-thread stack slot\n");

        if (!address_space_release(
                user_space))
        {
            for (;;)
                __asm__ volatile(
                    "cli; hlt");
        }

        kfree(
            launch);

        return 0;
    }

    if (main_stack_slot.stack_bottom !=
            PROCESS_USER_STACK_ADDRESS ||
        main_stack_slot.stack_top !=
            PROCESS_USER_STACK_TOP)
    {
        log_error(
            "spawn: ELF stack does not match "
            "address-space slot 0\n");

        for (;;)
            __asm__ volatile(
                "cli; hlt");
    }

    /*
     * ----------------------------------------------------------
     * STEP 9
     * Create process with REAL parent PID.
     * ----------------------------------------------------------
     */
    process_t *process =
        process_create(
            user_space,
            parent_pid);

    if (process == NULL)
    {
        log_error(
            "spawn: failed creating "
            "process for %s\n",
            path);

        if (!address_space_release(
                user_space))
        {
            for (;;)
                __asm__ volatile(
                    "cli; hlt");
        }

        kfree(
            launch);

        return 0;
    }

    process_id_t pid =
        process_id(
            process);

    /*
     * ----------------------------------------------------------
     * STEP 10
     * Create unpublished main task.
     * ----------------------------------------------------------
     */
    task_t *task =
        task_create_user_unpublished(
            process_user_task_entry,
            launch,
            user_space,
            SCHED_POLICY_REALTIME);

    if (task == NULL)
    {
        log_error(
            "spawn: failed creating unpublished "
            "userspace task for %s\n",
            path);

        process_release(
            process);

        process =
            NULL;

        if (!address_space_release(
                user_space))
        {
            for (;;)
                __asm__ volatile(
                    "cli; hlt");
        }

        kfree(
            launch);

        return 0;
    }

    if (task->address_space !=
            user_space ||
        address_space_page_directory(
            task->address_space) !=
            user_directory)
    {
        log_error(
            "spawn: task address-space "
            "ownership mismatch\n");

        for (;;)
            __asm__ volatile(
                "cli; hlt");
    }

    /*
     * ----------------------------------------------------------
     * STEP 11
     * Bind main task to process.
     * ----------------------------------------------------------
     */
    if (!task_bind_process(
            task,
            process))
    {
        log_error(
            "spawn: failed binding tid=%u "
            "to pid=%u\n",
            (unsigned)task->id,
            (unsigned)pid);

        for (;;)
            __asm__ volatile(
                "cli; hlt");
    }

    if (task->process !=
            process ||
        process_address_space(
            process) !=
            task->address_space ||
        process_thread_count(
            process) !=
            1u ||
        process_state(
            process) !=
            PROCESS_RUNNING)
    {
        log_error(
            "spawn: task/process ownership "
            "mismatch\n");

        for (;;)
            __asm__ volatile(
                "cli; hlt");
    }

    /*
     * ----------------------------------------------------------
     * STEP 12
     * Copy scheduler-visible values before publication.
     * ----------------------------------------------------------
     */
    task_id_t tid =
        task->id;

    uintptr_t task_directory =
        address_space_page_directory(
            task->address_space);

    uintptr_t user_entry =
        launch->entry;

    uintptr_t user_esp =
        launch->stack_pointer;

    log_success(
        "spawn: prepared %s "
        "pid=%u parent=%u tid=%u "
        "CR3=0x%lx entry=0x%lx "
        "ESP=0x%lx stack=%lu KiB\n",
        path,
        (unsigned)pid,
        (unsigned)parent_pid,
        (unsigned)tid,
        (unsigned long)
            task_directory,
        (unsigned long)
            user_entry,
        (unsigned long)
            user_esp,
        (unsigned long)(PROCESS_USER_STACK_SIZE /
                        1024u));

    /*
     * ----------------------------------------------------------
     * STEP 13
     * Drop creator process reference.
     * ----------------------------------------------------------
     */
    process_release(
        process);

    process =
        NULL;

    /*
     * ----------------------------------------------------------
     * STEP 14
     * Drop creator address-space reference.
     * ----------------------------------------------------------
     */
    if (!address_space_release(
            user_space))
    {
        log_error(
            "spawn: failed releasing "
            "creator address-space reference\n");

        for (;;)
            __asm__ volatile(
                "cli; hlt");
    }

    user_space =
        NULL;

    /*
     * ----------------------------------------------------------
     * STEP 15
     * Publish task.
     * ----------------------------------------------------------
     */
    task_publish(
        task);

    log_success(
        "spawn: published %s "
        "pid=%u parent=%u tid=%u\n",
        path,
        (unsigned)pid,
        (unsigned)parent_pid,
        (unsigned)tid);

    /*
     * Userspace process creation returns PID.
     *
     * TID remains an internal scheduler/thread identity.
     * Process-control APIs such as waitpid() operate on PID.
     */
    return pid;
}
