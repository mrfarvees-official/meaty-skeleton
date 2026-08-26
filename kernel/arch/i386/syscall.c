#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kernel/tty.h>
#include <kernel/keyboard.h>
#include <kernel/task.h>
#include <kernel/usercopy.h>
#include <kernel/user_thread.h>
#include <kernel/process.h>
#include <kernel/spawn.h>
#include <kernel/paging.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>

#include <kernel/gui/terminal_session.h>

#include "interrupts.h"
#include "syscall.h"

#define SYSCALL_DEBUG_WRITE_MAX 128u

#define SYSCALL_SPAWN_PATH_MAX 256u
#define SYSCALL_SPAWN_ARGC_MAX 16u
#define SYSCALL_SPAWN_ARG_MAX 128u

#define SYSCALL_STDIO_MAX 128u

#define SYSCALL_OPEN_PATH_MAX 256u

static bool syscall_copy_user_string(
    char *kernel_buffer,
    size_t capacity,
    const char *user_string)
{
    if (kernel_buffer == NULL ||
        capacity == 0u ||
        user_string == NULL)
    {
        return false;
    }

    for (size_t index = 0;
         index < capacity;
         ++index)
    {
        char character =
            '\0';

        if (!copy_from_user(
                &character,
                user_string + index,
                sizeof(character)))
        {
            return false;
        }

        kernel_buffer[index] =
            character;

        if (character ==
            '\0')
        {
            return true;
        }
    }

    /*
     * String did not terminate inside the allowed buffer.
     */
    kernel_buffer[capacity - 1u] =
        '\0';

    return false;
}

static int32_t syscall_read_stdio(
    uint32_t fd,
    uint32_t user_buffer_address,
    uint32_t requested_length)
{
    size_t length =
        (size_t)requested_length;

    if (length == 0u)
        return 0;

    if (length >
        SYSCALL_STDIO_MAX)
    {
        return I386_SYSCALL_ERROR_INVALID_LENGTH;
    }

    void *user_buffer =
        (void *)(uintptr_t)
            user_buffer_address;

    if (user_buffer == NULL)
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    char buffer[SYSCALL_STDIO_MAX];

    /*
     * Validate userspace destination before consuming input.
     */
    if (!copy_from_user(
            buffer,
            user_buffer,
            length) ||
        !copy_to_user(
            user_buffer,
            buffer,
            length))
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    if (fd == 0u)
    {
        task_t *task =
            task_current();

        gui_terminal_session_t *session =
            NULL;

        if (task != NULL &&
            task->process != NULL)
        {
            session =
                gui_terminal_session_for_process(
                    task->process);
        }

        size_t received =
            0u;

        if (session != NULL)
        {
            received =
                gui_terminal_session_read(
                    session,
                    buffer,
                    length);
        }
        else
        {
            /*
             * Preserve legacy console behavior for programs not
             * attached to a GUI Terminal.
             */
            while (received <
                   length)
            {
                char character =
                    '\0';

                if (!keyboard_read_character(
                        &character))
                {
                    break;
                }

                buffer[received++] =
                    character;
            }
        }

        if (received == 0u)
            return 0;

        if (!copy_to_user(
                user_buffer,
                buffer,
                received))
        {
            return I386_SYSCALL_ERROR_BAD_ADDRESS;
        }

        return (int32_t)received;
    }

    if (fd >=
        KERNEL_FD_FIRST)
    {
        size_t bytes_read =
            0u;

        if (kernel_fd_read(
                (int)fd,
                buffer,
                length,
                &bytes_read) != 0)
        {
            return I386_SYSCALL_ERROR_BAD_FD;
        }

        if (bytes_read == 0u)
            return 0;

        if (bytes_read >
            length)
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        if (!copy_to_user(
                user_buffer,
                buffer,
                bytes_read))
        {
            return I386_SYSCALL_ERROR_BAD_ADDRESS;
        }

        return (int32_t)bytes_read;
    }

    return I386_SYSCALL_ERROR_BAD_FD;
}

static int32_t syscall_write_stdio(
    uint32_t fd,
    uint32_t user_buffer_address,
    uint32_t requested_length)
{
    size_t length =
        (size_t)requested_length;

    if (length == 0u)
        return 0;

    if (length >
        SYSCALL_STDIO_MAX)
    {
        return I386_SYSCALL_ERROR_INVALID_LENGTH;
    }

    const void *user_buffer =
        (const void *)(uintptr_t)
            user_buffer_address;

    if (user_buffer == NULL)
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    char buffer[SYSCALL_STDIO_MAX];

    if (!copy_from_user(
            buffer,
            user_buffer,
            length))
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    if (fd == 1u ||
        fd == 2u)
    {
        task_t *task =
            task_current();

        gui_terminal_session_t *session =
            NULL;

        if (task != NULL &&
            task->process != NULL)
        {
            session =
                gui_terminal_session_for_process(
                    task->process);
        }

        if (session != NULL)
        {
            gui_terminal_session_write(
                session,
                buffer,
                length);
        }
        else
        {
            /*
             * Kernel/legacy console endpoint remains available.
             */
            terminal_write(
                buffer,
                length);
        }

        return (int32_t)length;
    }

    if (fd >=
        KERNEL_FD_FIRST)
    {
        size_t bytes_written =
            0u;

        if (kernel_fd_write(
                (int)fd,
                buffer,
                length,
                &bytes_written) != 0)
        {
            return I386_SYSCALL_ERROR_BAD_FD;
        }

        if (bytes_written >
            length)
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        return (int32_t)bytes_written;
    }

    return I386_SYSCALL_ERROR_BAD_FD;
}

static int32_t syscall_open_file(
    uint32_t user_path_address,
    uint32_t flags)
{
    task_t *task =
        task_current();

    if (task == NULL ||
        task->process == NULL)
    {
        return I386_SYSCALL_ERROR_INVALID_STATE;
    }

    const char *user_path =
        (const char *)(uintptr_t)
            user_path_address;

    if (user_path == NULL)
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    char supplied_path[SYSCALL_OPEN_PATH_MAX];

    if (!syscall_copy_user_string(
            supplied_path,
            sizeof(supplied_path),
            user_path))
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    char absolute_path[PROCESS_PATH_MAX];

    if (!process_resolve_path(
            task->process,
            supplied_path,
            absolute_path,
            sizeof(absolute_path)))
    {
        return I386_SYSCALL_ERROR_INVALID_LENGTH;
    }

    int fd =
        kernel_fd_open(
            absolute_path,
            flags);

    if (fd < 0)
    {
        return I386_SYSCALL_ERROR_BAD_FD;
    }

    return (int32_t)fd;
}

static int32_t syscall_chdir(
    uint32_t user_path_address)
{
    task_t *task =
        task_current();

    if (task == NULL ||
        task->process == NULL)
    {
        return I386_SYSCALL_ERROR_INVALID_STATE;
    }

    const char *user_path =
        (const char *)(uintptr_t)
            user_path_address;

    if (user_path == NULL)
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    char supplied_path[PROCESS_PATH_MAX];

    if (!syscall_copy_user_string(
            supplied_path,
            sizeof(supplied_path),
            user_path))
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    char absolute_path[PROCESS_PATH_MAX];

    if (!process_resolve_path(
            task->process,
            supplied_path,
            absolute_path,
            sizeof(absolute_path)))
    {
        return I386_SYSCALL_ERROR_INVALID_LENGTH;
    }

    vnode_t *node =
        NULL;

    if (vfs_lookup(
            absolute_path,
            &node) != 0 ||
        node == NULL)
    {
        return I386_SYSCALL_ERROR_INVALID_STATE;
    }

    bool is_directory =
        node->type ==
        VNODE_DIRECTORY;

    vnode_unref(
        node);

    if (!is_directory)
    {
        return I386_SYSCALL_ERROR_INVALID_STATE;
    }

    if (!process_set_cwd(
            task->process,
            absolute_path))
    {
        return I386_SYSCALL_ERROR_INVALID_STATE;
    }

    return 0;
}

static int32_t syscall_getcwd(
    uint32_t user_buffer_address,
    uint32_t capacity_value)
{
    task_t *task =
        task_current();

    if (task == NULL ||
        task->process == NULL)
    {
        return I386_SYSCALL_ERROR_INVALID_STATE;
    }

    char *user_buffer =
        (char *)(uintptr_t)
            user_buffer_address;

    size_t capacity =
        (size_t)capacity_value;

    if (user_buffer == NULL)
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    if (capacity == 0u)
    {
        return I386_SYSCALL_ERROR_INVALID_LENGTH;
    }

    char cwd[PROCESS_PATH_MAX];

    if (!process_get_cwd(
            task->process,
            cwd,
            sizeof(cwd)))
    {
        return I386_SYSCALL_ERROR_INVALID_STATE;
    }

    size_t length =
        strlen(cwd);

    if (length + 1u >
        capacity)
    {
        return I386_SYSCALL_ERROR_INVALID_LENGTH;
    }

    if (!copy_to_user(
            user_buffer,
            cwd,
            length + 1u))
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    return (int32_t)length;
}

static int32_t syscall_close_file(
    uint32_t fd)
{
    if (fd <
        KERNEL_FD_FIRST)
    {
        return I386_SYSCALL_ERROR_BAD_FD;
    }

    if (kernel_fd_close(
            (int)fd) != 0)
    {
        return I386_SYSCALL_ERROR_BAD_FD;
    }

    return 0;
}

static int32_t syscall_read_key_event(void)
{
    keyboard_event_t event;

    /*
     * The shell consumes the keyboard EVENT queue as its ordered
     * interactive input stream.
     *
     * Do not touch the independent character queue here.
     */
    while (keyboard_read_event(
        &event))
    {
        /*
         * Shell input only cares about key presses.
         */
        if (!event.pressed)
        {
            continue;
        }

        /*
         * Ordinary character.
         */
        if (event.character != '\0')
        {
            return (int32_t)(uint8_t)event.character;
        }

        switch (event.key)
        {
        case KEY_ARROW_LEFT:
            return I386_KEY_EVENT_LEFT;

        case KEY_ARROW_RIGHT:
            return I386_KEY_EVENT_RIGHT;

        case KEY_ARROW_UP:
            return I386_KEY_EVENT_UP;

        case KEY_ARROW_DOWN:
            return I386_KEY_EVENT_DOWN;

        case KEY_HOME:
            return I386_KEY_EVENT_HOME;

        case KEY_END:
            return I386_KEY_EVENT_END;

        case KEY_DELETE:
            return I386_KEY_EVENT_DELETE;

        default:
            break;
        }
    }

    return 0;
}

static int32_t syscall_readdir_file(
    uint32_t fd,
    uint32_t user_entry_address)
{
    if (fd <
        KERNEL_FD_FIRST)
    {
        return I386_SYSCALL_ERROR_BAD_FD;
    }

    i386_syscall_dirent_t *user_entry =
        (i386_syscall_dirent_t *)(uintptr_t)
            user_entry_address;

    if (user_entry == NULL)
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    /*
     * Validate the complete destination before advancing the
     * directory descriptor's cursor.
     */
    i386_syscall_dirent_t probe;

    if (!copy_from_user(
            &probe,
            user_entry,
            sizeof(probe)))
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    if (!copy_to_user(
            user_entry,
            &probe,
            sizeof(probe)))
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    vfs_dirent_t entry;

    int result =
        kernel_fd_readdir(
            (int)fd,
            &entry);

    if (result < 0)
    {
        return I386_SYSCALL_ERROR_BAD_FD;
    }

    if (result == 0)
        return 0;

    if (entry.inode >
        UINT32_MAX)
    {
        return I386_SYSCALL_ERROR_INVALID_STATE;
    }

    i386_syscall_dirent_t output;

    memset(
        &output,
        0,
        sizeof(output));

    output.inode =
        (uint32_t)entry.inode;

    switch (entry.type)
    {
    case VNODE_REGULAR:
        output.type =
            I386_DIRENT_TYPE_REGULAR;
        break;

    case VNODE_DIRECTORY:
        output.type =
            I386_DIRENT_TYPE_DIRECTORY;
        break;

    default:
        return I386_SYSCALL_ERROR_INVALID_STATE;
    }

    size_t name_length =
        strlen(entry.name);

    if (name_length >=
        sizeof(output.name))
    {
        return I386_SYSCALL_ERROR_INVALID_LENGTH;
    }

    memcpy(
        output.name,
        entry.name,
        name_length + 1u);

    if (!copy_to_user(
            user_entry,
            &output,
            sizeof(output)))
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    return 1;
}

static int32_t syscall_mkdir(
    uint32_t user_path_address)
{
    task_t *task =
        task_current();

    if (task == NULL ||
        task->process == NULL)
    {
        return I386_SYSCALL_ERROR_INVALID_STATE;
    }

    const char *user_path =
        (const char *)(uintptr_t)
            user_path_address;

    if (user_path == NULL)
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    char supplied_path[PROCESS_PATH_MAX];

    if (!syscall_copy_user_string(
            supplied_path,
            sizeof(supplied_path),
            user_path))
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    char absolute_path[PROCESS_PATH_MAX];

    if (!process_resolve_path(
            task->process,
            supplied_path,
            absolute_path,
            sizeof(absolute_path)))
    {
        return I386_SYSCALL_ERROR_INVALID_LENGTH;
    }

    if (vfs_mkdir(
            absolute_path) != 0)
    {
        return I386_SYSCALL_ERROR_INVALID_STATE;
    }

    return 0;
}

static int32_t syscall_dispatch(
    uint32_t number,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t arg2,
    uint32_t arg3,
    uint32_t arg4)
{
    (void)arg3;
    (void)arg4;

    switch (number)
    {
    case I386_SYSCALL_GETTID:
    {
        task_t *task =
            task_current();

        if (task == NULL)
            return I386_SYSCALL_ERROR_INVALID_STATE;

        return (int32_t)task->id;
    }

    case I386_SYSCALL_USERCOPY_TEST:
    {
        const void *user_input =
            (const void *)(uintptr_t)arg0;

        void *user_output =
            (void *)(uintptr_t)arg1;

        size_t length =
            (size_t)arg2;

        if (length != 4u)
            return I386_SYSCALL_ERROR_BAD_ADDRESS;

        char input[4];

        if (!copy_from_user(
                input,
                user_input,
                sizeof(input)))
        {
            printf(
                "U2d: copy_from_user rejected 0x%lx\n",
                (unsigned long)arg0);

            return I386_SYSCALL_ERROR_BAD_ADDRESS;
        }

        if (input[0] != 'M' ||
            input[1] != 'A' ||
            input[2] != 'T' ||
            input[3] != 'E')
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        static const char reply[4] =
            {'O', 'K', 'A', 'Y'};

        if (!copy_to_user(
                user_output,
                reply,
                sizeof(reply)))
        {
            return I386_SYSCALL_ERROR_BAD_ADDRESS;
        }

        return 4;
    }

    case I386_SYSCALL_DEBUG_WRITE:
    {
        const void *user_buffer =
            (const void *)(uintptr_t)arg0;

        size_t length =
            (size_t)arg1;

        if (length == 0u)
            return 0;

        if (length >
            SYSCALL_DEBUG_WRITE_MAX)
        {
            return I386_SYSCALL_ERROR_INVALID_LENGTH;
        }

        char buffer[SYSCALL_DEBUG_WRITE_MAX];

        if (!copy_from_user(
                buffer,
                user_buffer,
                length))
        {
            return I386_SYSCALL_ERROR_BAD_ADDRESS;
        }

        terminal_write(
            buffer,
            length);

        return (int32_t)length;
    }

    case I386_SYSCALL_EXIT:
    {
        task_t *task =
            task_current();

        if (task == NULL)
        {
            printf(
                "[EXIT ERROR] task_current() == NULL\n");

            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        if (task->process == NULL)
        {
            printf(
                "[EXIT ERROR] tid=%u has NULL process "
                "CR3=0x%lx\n",
                (unsigned)task->id,
                (unsigned long)
                    paging_current_directory());

            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        process_t *process =
            task->process;

        process_id_t pid =
            process_id(
                process);

        process_state_t state =
            process_state(
                process);

        int status =
            (int)(int32_t)arg0;

        // printf(
        //     "[EXIT] tid=%u pid=%u state=%u "
        //     "status=%d CR3=0x%lx\n",
        //     (unsigned)task->id,
        //     (unsigned)pid,
        //     (unsigned)state,
        //     status,
        //     (unsigned long)
        //         paging_current_directory());

        if (!process_set_exit_status(
                process,
                status))
        {
            printf(
                "[EXIT ERROR] process_set_exit_status "
                "failed tid=%u pid=%u state=%u\n",
                (unsigned)task->id,
                (unsigned)pid,
                (unsigned)state);

            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        // printf(
        //     "[EXIT] tid=%u pid=%u calling task_exit()\n",
        //     (unsigned)task->id,
        //     (unsigned)pid);

        task_exit();

        /*
         * task_exit() must never return.
         */
        printf(
            "[EXIT FATAL] task_exit returned "
            "tid=%u pid=%u\n",
            (unsigned)task->id,
            (unsigned)pid);

        for (;;)
        {
            __asm__ volatile(
                "cli; hlt");
        }
    }

    case I386_SYSCALL_THREAD_CREATE:
    {
        uintptr_t entry =
            (uintptr_t)arg0;

        task_id_t tid =
            user_thread_create_current(
                entry);

        if (tid == 0u)
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        return (int32_t)tid;
    }

    case I386_SYSCALL_YIELD:
    {
        task_t *task =
            task_current();

        if (task == NULL)
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        task_yield();

        return 0;
    }

    case I386_SYSCALL_WAITPID:
    {
        task_t *task =
            task_current();

        if (task == NULL ||
            task->process == NULL ||
            task->address_space == NULL)
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        process_id_t child_pid =
            (process_id_t)arg0;

        if (child_pid ==
            PROCESS_ID_INVALID)
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        int *user_status =
            (int *)(uintptr_t)arg1;

        /*
         * ----------------------------------------------------------
         * STEP 1
         * Validate userspace destination while the caller's userspace
         * CR3 is still active.
         * ----------------------------------------------------------
         *
         * A successful process_waitpid() may release the final reference
         * to the child process and therefore permanently consume it.
         *
         * Validate the destination before doing that.
         */
        if (user_status != NULL)
        {
            int original_status =
                0;

            if (!copy_from_user(
                    &original_status,
                    user_status,
                    sizeof(original_status)))
            {
                return I386_SYSCALL_ERROR_BAD_ADDRESS;
            }

            if (!copy_to_user(
                    user_status,
                    &original_status,
                    sizeof(original_status)))
            {
                return I386_SYSCALL_ERROR_BAD_ADDRESS;
            }
        }

        /*
         * ----------------------------------------------------------
         * STEP 2
         * Capture caller/kernel CR3.
         * ----------------------------------------------------------
         */
        uintptr_t caller_directory =
            address_space_page_directory(
                task->address_space);

        uintptr_t kernel_directory =
            paging_kernel_directory();

        if (caller_directory == 0u ||
            kernel_directory == 0u ||
            paging_current_directory() !=
                caller_directory)
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        /*
         * ----------------------------------------------------------
         * STEP 3
         * Perform process collection from canonical kernel CR3.
         * ----------------------------------------------------------
         *
         * process_waitpid() can drop the parent's ownership reference.
         *
         * If that is the child's final process reference,
         * process_release() releases the child's address_space_t.
         *
         * Final address-space destruction intentionally requires the
         * canonical kernel page directory to be active.
         */
        if (!paging_switch_directory(
                kernel_directory))
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        int status =
            0;

        bool collected =
            process_waitpid(
                task->process,
                child_pid,
                &status);

        /*
         * ----------------------------------------------------------
         * STEP 4
         * Restore the caller before touching userspace or returning.
         * ----------------------------------------------------------
         */
        if (!paging_switch_directory(
                caller_directory))
        {
            /*
             * Returning to ring 3 with the wrong CR3 would be fatal.
             */
            for (;;)
            {
                __asm__ volatile(
                    "cli; hlt");
            }
        }

        /*
         * Child is either:
         *
         *     - still running
         *     - not our child
         *     - already collected
         *
         * Failed waitpid must leave status unchanged.
         */
        if (!collected)
        {
            return 0;
        }

        /*
         * ----------------------------------------------------------
         * STEP 5
         * Copy status after restoring caller userspace CR3.
         * ----------------------------------------------------------
         *
         * Destination was already validated before collection.
         */
        if (user_status != NULL)
        {
            if (!copy_to_user(
                    user_status,
                    &status,
                    sizeof(status)))
            {
                /*
                 * This should only become possible if the caller's
                 * userspace mappings change concurrently.
                 *
                 * The child has already been collected, so report the
                 * address failure rather than pretending collection did
                 * not occur.
                 */
                return I386_SYSCALL_ERROR_BAD_ADDRESS;
            }
        }

        return 1;
    }

    case I386_SYSCALL_SPAWN:
    {
        task_t *task =
            task_current();

        if (task == NULL ||
            task->process == NULL ||
            task->address_space == NULL)
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        const char *user_path =
            (const char *)(uintptr_t)arg0;

        size_t argc =
            (size_t)arg1;

        const char *const *user_argv =
            (const char *const *)(uintptr_t)arg2;

        if (user_path == NULL ||
            user_argv == NULL)
        {
            return I386_SYSCALL_ERROR_BAD_ADDRESS;
        }

        if (argc == 0u ||
            argc >
                SYSCALL_SPAWN_ARGC_MAX)
        {
            return I386_SYSCALL_ERROR_INVALID_LENGTH;
        }

        /*
         * Everything supplied by userspace must be copied before
         * switching away from the caller's page directory.
         */
        char supplied_path[SYSCALL_SPAWN_PATH_MAX];

        if (!syscall_copy_user_string(
                supplied_path,
                sizeof(supplied_path),
                user_path))
        {
            return I386_SYSCALL_ERROR_BAD_ADDRESS;
        }

        char kernel_path[PROCESS_PATH_MAX];

        if (!process_resolve_path(
                task->process,
                supplied_path,
                kernel_path,
                sizeof(kernel_path)))
        {
            return I386_SYSCALL_ERROR_INVALID_LENGTH;
        }

        char kernel_argument_storage
            [SYSCALL_SPAWN_ARGC_MAX]
            [SYSCALL_SPAWN_ARG_MAX];

        const char *kernel_argv[SYSCALL_SPAWN_ARGC_MAX];

        for (size_t index = 0;
             index < argc;
             ++index)
        {
            const char *user_argument =
                NULL;

            if (!copy_from_user(
                    &user_argument,
                    user_argv + index,
                    sizeof(user_argument)))
            {
                return I386_SYSCALL_ERROR_BAD_ADDRESS;
            }

            if (user_argument == NULL)
            {
                return I386_SYSCALL_ERROR_BAD_ADDRESS;
            }

            if (!syscall_copy_user_string(
                    kernel_argument_storage[index],
                    sizeof(
                        kernel_argument_storage[index]),
                    user_argument))
            {
                return I386_SYSCALL_ERROR_BAD_ADDRESS;
            }

            kernel_argv[index] =
                kernel_argument_storage[index];
        }

        /*
         * Verify argv[argc] is actually NULL.
         */
        const char *user_terminator =
            (const char *)(uintptr_t)1u;

        if (!copy_from_user(
                &user_terminator,
                user_argv + argc,
                sizeof(user_terminator)))
        {
            return I386_SYSCALL_ERROR_BAD_ADDRESS;
        }

        if (user_terminator != NULL)
        {
            return I386_SYSCALL_ERROR_INVALID_LENGTH;
        }

        uintptr_t caller_directory =
            address_space_page_directory(
                task->address_space);

        uintptr_t kernel_directory =
            paging_kernel_directory();

        if (caller_directory == 0u ||
            kernel_directory == 0u ||
            paging_current_directory() !=
                caller_directory)
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        /*
         * process_spawn_user() currently performs ELF loading from
         * canonical kernel CR3.
         *
         * All userspace pointers have already been copied above.
         */
        if (!paging_switch_directory(
                kernel_directory))
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        process_id_t child_pid =
            process_spawn_user(
                kernel_path,
                argc,
                kernel_argv);

        /*
         * We MUST restore the caller's CR3 before returning through
         * IRET to ring 3.
         */
        if (!paging_switch_directory(
                caller_directory))
        {
            for (;;)
            {
                __asm__ volatile(
                    "cli; hlt");
            }
        }

        if (child_pid ==
            PROCESS_ID_INVALID)
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        return (int32_t)child_pid;
    }

    case I386_SYSCALL_READ:
    {
        return syscall_read_stdio(
            arg0,
            arg1,
            arg2);
    }

    case I386_SYSCALL_WRITE:
    {
        return syscall_write_stdio(
            arg0,
            arg1,
            arg2);
    }

    case I386_SYSCALL_OPEN:
    {
        return syscall_open_file(
            arg0,
            arg1);
    }

    case I386_SYSCALL_CLOSE:
    {
        return syscall_close_file(
            arg0);
    }

    case I386_SYSCALL_KEY_EVENT:
    {
        return syscall_read_key_event();
    }

    case I386_SYSCALL_CHDIR:
    {
        return syscall_chdir(
            arg0);
    }

    case I386_SYSCALL_GETCWD:
    {
        return syscall_getcwd(
            arg0,
            arg1);
    }

    case I386_SYSCALL_READDIR:
    {
        return syscall_readdir_file(
            arg0,
            arg1);
    }

    case I386_SYSCALL_MKDIR:
    {
        return syscall_mkdir(
            arg0);
    }

    default:
        return I386_SYSCALL_ERROR_NO_SUCH_SYSCALL;
    }
}

static void syscall_handler(
    struct interrupt_frame *frame)
{
    if (frame == NULL)
        return;

    /*
     * For U2a we expect syscalls only from CPL3.
     */
    if ((frame->cs & 3u) != 3u)
    {
        printf(
            "U2: rejected syscall from CS=0x%lx\n",
            (unsigned long)frame->cs);

        frame->eax =
            (uint32_t)I386_SYSCALL_ERROR_INVALID_STATE;
        return;
    }

    uint32_t number =
        frame->eax;

    int32_t result =
        syscall_dispatch(
            frame->eax,
            frame->ebx,
            frame->ecx,
            frame->edx,
            frame->esi,
            frame->edi);

    frame->eax =
        (uint32_t)result;
}

bool syscall_initialize(void)
{
    return interrupt_register_handler(
               I386_SYSCALL_VECTOR,
               syscall_handler) == 0;
}