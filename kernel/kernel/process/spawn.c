#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/spawn.h>

#include <kernel/elf.h>
#include <kernel/heap.h>
#include <kernel/paging.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/user_image.h>
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

static void process_user_task_entry(void *argument)
{
    process_user_launch_context_t *launch = (process_user_launch_context_t *)argument;

    if (launch == NULL)
    {
        log_error("spawn: missign user launch context\n");
        task_exit();
    }

    uintptr_t entry = launch->entry;
    uintptr_t stack_pointer = launch->stack_pointer;

    /**
     * We no longer need this kernel heap object.
     *
     * arch_enter_user() never returns, so it must be freed BEFORE
     * entering userspace.
     */
    kfree(launch);

    /**
     * Defensive validation.
     */
    if (entry == 0 ||
        stack_pointer < PROCESS_USER_STACK_ADDRESS ||
        stack_pointer >= PROCESS_USER_STACK_TOP)
    {
        log_error("spawn: invalid user entry/stack entry=0x%lx esp=0x%lx\n", (unsigned long)entry, (unsigned long)stack_pointer);
        task_exit();
    }

    task_t *task = task_current();

    if (task == NULL)
    {
        log_error("spawn: no current task\n");
        task_exit();
    }

    uintptr_t current_directory = paging_current_directory();

    /**
     * Userspace task must
     *
     * 1. own its page directory
     * 2. actually have that CR3 active
     * 3. not be running in the kernel page directory
     */
    if (!task->owns_page_directory ||
        current_directory != task->page_directory ||
        current_directory == paging_kernel_directory())
    {
        log_error("spawn: invalid user address space tid=%u CR3=0x%lx expected=0x%lx\n", (unsigned)task->id, (unsigned long)current_directory, (unsigned long)task->page_directory);
        task_exit();
    }

    log_success("spawn: entering userspace tid=%u entry=0x%lx esp=0x%lx\n", (unsigned)task->id, (unsigned long)entry, (unsigned long)stack_pointer);

    /**
     * Does not return
     */
    arch_enter_user(entry, stack_pointer);
}

/**
 * -----------------------------------------------------------------------------
 * PROCESS SPAWN
 * -----------------------------------------------------------------------------
 */
task_id_t process_spawn_user(
    const char *path,
    size_t argc,
    const char *const argv[])
{
    // Basic argument validation.
    if (path == NULL || argc == 0 || argv == NULL)
    {
        log_error("spawn: invalid arguments\n");
        return 0;
    }

    /**
     * ELF loading currently requires the canonical kernel page
     * directory to be active
     */
    uintptr_t kernel_directory = paging_kernel_directory();

    if (kernel_directory == 0 || paging_current_directory() != kernel_directory)
    {
        log_error("spawn: kernel CR3 is not active\n");
        return 0;
    }

    /**
     * STEP 1
     *
     * Open executable throught VFS
     */
    file_t *file = NULL;

    if (vfs_open(path, VFS_OPEN_READ, &file) != 0 || file == NULL)
    {
        log_error("spawn: failed opening %s\n", path);
        return 0;
    }

    // Only regular files can currently be executed
    if (file->vnode == NULL || file->vnode->type != VNODE_REGULAR)
    {
        log_error("spawn: %s is not a regular file\n", path);
        vfs_close(file);
        return 0;
    }

    /**
     * STEP 2
     *
     * Validate executable size.
     */
    uint64_t executable_size_64 = file->vnode->size;
    if (executable_size_64 == 0)
    {
        log_error("spawn: executable %s is empty\n", path);
        vfs_close(file);
        return 0;
    }

    if (executable_size_64 > (uint64_t)SIZE_MAX ||
        executable_size_64 > (uint64_t)PROCESS_MAX_EXECUTABLE_SIZE)
    {
        log_error("spawn: executable %s has invalid size %llu bytes\n", path, (unsigned long long)executable_size_64);
        vfs_close(file);
        return 0;
    }

    size_t executable_size = (size_t)executable_size_64;

    /**
     * STEP 3
     *
     * Allocate temporary kernel buffer for the complete ELF file.
     */
    uint8_t *executable_data = kmalloc(executable_size);
    if (executable_data == NULL)
    {
        log_error("spawn: failed allocating %lu-byte ELF buffer\n", (unsigned long)executable_size);
        vfs_close(file);
        return 0;
    }

    /**
     * STEP 4
     *
     * Read complete executable
     */
    size_t total_read = 0;
    while (total_read < executable_size)
    {
        size_t bytes_read = 0;
        if (vfs_read(
                file,
                executable_data + total_read,
                executable_size - total_read,
                &bytes_read) != 0)
        {
            log_error("spawn: VFS read failed for %s at offset %lu\n", path, (unsigned long)total_read);
            kfree(executable_data);
            vfs_close(file);
            return 0;
        }

        /**
         * A successful zero-byte read before reaching vnode->size
         * means the file changed/truncated or filesystem returned
         * inconsistent information.
         */
        if (bytes_read == 0)
        {
            log_error("spawn: unexpected EOF for %s at %lu/%lu\n", path, (unsigned long)total_read, (unsigned long)executable_size);
            kfree(executable_data);
            vfs_close(file);
            return 0;
        }

        /**
         * Defensive VFS check
         */
        if (bytes_read > executable_size - total_read)
        {
            log_error("spawn: VFS returned invalid read length for %s\n", path);
            kfree(executable_data);
            vfs_close(file);
            return 0;
        }

        total_read += bytes_read;
    }

    // File object is no longer needed.
    vfs_close(file);

    file = NULL;

    if (total_read != executable_size)
    {
        log_error("spawn: executable read length mismatch for %s\n", path);
        kfree(executable_data);
        return 0;
    }

    log_success("spawn: loaded %s (%lu bytes)\n", path, (unsigned long)executable_size);

    /**
     * STEP 5
     *
     * ELF loader creates
     *
     *      - private page directory
     *      - ELF PT_LOAD mappings
     *      - BSS pages
     *      - 1 MiB userspace stack
     *      - argc/argv
     *
     * image.stack_top will contain the prepared INITIAL ESP.
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
        log_error("spawn: ELF loader rejected %s\n", path);
        kfree(executable_data);
        return 0;
    }

    /**
     * ELF PT_LOAD contents and argv have now been copied into private
     * userspace physical frames.
     *
     * The original kernel-side file buffer can be released.
     */
    kfree(executable_data);
    executable_data = NULL;

    /**
     * STEP 6
     *
     * Validate prepared ELF image.
     */
    if (image.page_directory == 0 ||
        image.entry == 0 ||
        image.stack_top < PROCESS_USER_STACK_ADDRESS ||
        image.stack_top >= PROCESS_USER_STACK_TOP)
    {
        log_error("spawn: ELF loader produced invalid image for %s\n", path);
        log_error("spawn: entry=0x%lx CR3=0x%lx ESP=0x%lx\n", (unsigned long)image.entry, (unsigned long)image.page_directory, (unsigned long)image.stack_top);
        user_image_destroy(&image);
        return 0;
    }

    /**
     * STEP 7
     *
     * Allocate launch information that survives until the scheduler
     * acutally starts this task.
     */
    process_user_launch_context_t *launch = kmalloc(sizeof(*launch));

    if (launch == NULL)
    {
        log_error("spawn: failed allocating launch context\n");
        user_image_destroy(&image);
        return 0;
    }

    launch->entry = image.entry;
    launch->stack_pointer = image.stack_top;

    uintptr_t user_directory = image.page_directory;

    /*
     * ------------------------------------------------------------------
     * STEP 8
     *
     * Create the task, but DO NOT publish it yet.
     *
     * This prevents another CPU from running it while spawn.c
     * is still finishing the ownership setup.
     * ------------------------------------------------------------------
     */

    task_t *task =
        task_create_user_unpublished(
            process_user_task_entry,
            launch,
            user_directory,
            SCHED_POLICY_REALTIME);

    if (task == NULL)
    {
        log_error(
            "spawn: failed creating unpublished userspace task for %s\n",
            path);

        /*
         * Task creation failed.
         *
         * Ownership of the user image still belongs here.
         */
        kfree(
            launch);

        user_image_destroy(
            &image);

        return 0;
    }

    /*
     * The task exists but cannot run yet because its state
     * is still TASK_NEW.
     *
     * Therefore accessing task here is safe.
     */
    if (!task->owns_page_directory ||
        task->page_directory !=
            user_directory)
    {
        log_error(
            "spawn: task did not accept user address-space ownership\n");

        for (;;)
            __asm__ volatile(
                "cli; hlt");
    }

    /*
     * ------------------------------------------------------------------
     * STEP 9
     *
     * Transfer the user_image_t address-space ownership marker
     * completely to task_t.
     * ------------------------------------------------------------------
     */

    uintptr_t detached_directory =
        user_image_detach_directory(
            &image);

    if (detached_directory !=
        user_directory)
    {
        log_error(
            "spawn: address-space ownership transfer failed\n");

        for (;;)
            __asm__ volatile(
                "cli; hlt");
    }

    /*
     * ------------------------------------------------------------------
     * STEP 10
     *
     * SAVE EVERYTHING WE NEED BEFORE PUBLISHING.
     *
     * Once task_publish() runs, another CPU may execute and
     * eventually destroy task_t and launch.
     * ------------------------------------------------------------------
     */

    task_id_t tid =
        task->id;

    uintptr_t task_directory =
        task->page_directory;

    uintptr_t user_entry =
        launch->entry;

    uintptr_t user_esp =
        launch->stack_pointer;

    log_success(
        "spawn: prepared %s tid=%u CR3=0x%lx "
        "entry=0x%lx ESP=0x%lx stack=%lu KiB\n",
        path,
        (unsigned)tid,
        (unsigned long)
            task_directory,
        (unsigned long)
            user_entry,
        (unsigned long)
            user_esp,
        (unsigned long)(PROCESS_USER_STACK_SIZE / 1024u));

    /*
     * ------------------------------------------------------------------
     * STEP 11
     *
     * Publish the task.
     *
     * FROM THIS POINT:
     *
     *     DO NOT dereference task
     *     DO NOT dereference launch
     *
     * CPU 1 may already be running/freeing them.
     * ------------------------------------------------------------------
     */

    task_publish(
        task);

    /*
     * Only use local copied values such as tid here.
     */
    log_success(
        "spawn: published %s tid=%u\n",
        path,
        (unsigned)tid);

    return tid;
}