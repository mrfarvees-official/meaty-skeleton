#include <stdint.h>
#include <stdio.h>

#include <kernel/tty.h>
#include <kernel/keyboard.h>
#include <kernel/task.h>
#include <kernel/usercopy.h>
#include <kernel/user_thread.h>
#include <kernel/process.h>
#include <kernel/spawn.h>
#include <kernel/paging.h>

#include "interrupts.h"
#include "syscall.h"

#define SYSCALL_DEBUG_WRITE_MAX 128u

#define SYSCALL_SPAWN_PATH_MAX 256u
#define SYSCALL_SPAWN_ARGC_MAX 16u
#define SYSCALL_SPAWN_ARG_MAX 128u

#define SYSCALL_STDIO_MAX 128u

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
    /*
     * Shell v0:
     *
     *     fd 0 = keyboard character stream
     *
     * Nothing else is readable yet.
     */
    if (fd != 0u)
    {
        return I386_SYSCALL_ERROR_BAD_FD;
    }

    size_t length =
        (size_t)requested_length;

    /*
     * Standard read-style zero-length operation.
     *
     * Do not require a valid buffer when no bytes are requested.
     */
    if (length == 0u)
    {
        return 0;
    }

    if (length >
        SYSCALL_STDIO_MAX)
    {
        return I386_SYSCALL_ERROR_INVALID_LENGTH;
    }

    void *user_buffer =
        (void *)(uintptr_t)user_buffer_address;

    if (user_buffer == NULL)
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    char buffer[SYSCALL_STDIO_MAX];

    /*
     * ----------------------------------------------------------
     * Validate the complete userspace destination BEFORE blocking.
     * ----------------------------------------------------------
     *
     * copy_from_user() proves the range is mapped/user-readable.
     *
     * copy_to_user() additionally proves the complete range is
     * writable.
     *
     * Writing the original bytes straight back leaves the caller's
     * buffer unchanged.
     *
     * This prevents us from blocking for keyboard input, consuming
     * characters, and only afterwards discovering that the user's
     * destination is invalid.
     */
    if (!copy_from_user(
            buffer,
            user_buffer,
            length))
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    if (!copy_to_user(
            user_buffer,
            buffer,
            length))
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    /*
     * ----------------------------------------------------------
     * Block until at least one character exists.
     * ----------------------------------------------------------
     *
     * keyboard_wait_character() already sleeps the current task
     * through the semaphore/wait-queue infrastructure.
     *
     * Do not echo here.
     *
     * The future shell owns echo/backspace/line assembly.
     */
    if (!keyboard_wait_character(
            &buffer[0]))
    {
        return I386_SYSCALL_ERROR_INVALID_STATE;
    }

    size_t received =
        1u;

    /*
     * Once at least one byte has been obtained, drain anything that
     * is already queued without blocking again.
     *
     * This gives read()-style "up to count" behavior while ensuring
     * that a read requesting multiple bytes does not wait until all
     * requested bytes have arrived.
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

        buffer[received] =
            character;

        ++received;
    }

    /*
     * We validated the complete requested destination range before
     * consuming input.
     */
    if (!copy_to_user(
            user_buffer,
            buffer,
            received))
    {
        return I386_SYSCALL_ERROR_BAD_ADDRESS;
    }

    return (int32_t)received;
}

static int32_t syscall_write_stdio(
    uint32_t fd,
    uint32_t user_buffer_address,
    uint32_t requested_length)
{
    /*
     * Shell v0:
     *
     *     fd 1 = terminal
     *     fd 2 = terminal
     */
    if (fd != 1u &&
        fd != 2u)
    {
        return I386_SYSCALL_ERROR_BAD_FD;
    }

    size_t length =
        (size_t)requested_length;

    if (length == 0u)
    {
        return 0;
    }

    if (length >
        SYSCALL_STDIO_MAX)
    {
        return I386_SYSCALL_ERROR_INVALID_LENGTH;
    }

    const void *user_buffer =
        (const void *)(uintptr_t)user_buffer_address;

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

    terminal_write(
        buffer,
        length);

    return (int32_t)length;
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

        if (task == NULL ||
            task->process == NULL)
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        int status =
            (int)(int32_t)arg0;

        if (!process_set_exit_status(
                task->process,
                status))
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        task_exit();
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
        char kernel_path[SYSCALL_SPAWN_PATH_MAX];

        if (!syscall_copy_user_string(
                kernel_path,
                sizeof(kernel_path),
                user_path))
        {
            return I386_SYSCALL_ERROR_BAD_ADDRESS;
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