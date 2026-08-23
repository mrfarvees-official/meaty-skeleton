#include <stdint.h>
#include <stdio.h>

#include <kernel/tty.h>
#include <kernel/task.h>
#include <kernel/usercopy.h>
#include <kernel/user_thread.h>
#include <kernel/process.h>

#include "interrupts.h"
#include "syscall.h"

#define SYSCALL_DEBUG_WRITE_MAX 128u

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
            printf(
                "U2d: user input contents FAILED\n");

            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        static const char reply[4] =
            {'O', 'K', 'A', 'Y'};

        if (!copy_to_user(
                user_output,
                reply,
                sizeof(reply)))
        {
            printf(
                "U2d: copy_to_user rejected 0x%lx\n",
                (unsigned long)arg1);

            return I386_SYSCALL_ERROR_BAD_ADDRESS;
        }

        printf(
            "U2d: copied 4 bytes from/to user memory\n");

        return 4;
    }

    case I386_SYSCALL_DEBUG_WRITE:
    {
        const void *user_buffer =
            (const void *)(uintptr_t)arg0;

        size_t length =
            (size_t)arg1;

        if (length == 0)
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
            printf(
                "U2e: debug_write rejected user buffer 0x%lx\n",
                (unsigned long)arg0);

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
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        int status =
            (int)(int32_t)arg0;

        if (task->process == NULL)
        {
            return I386_SYSCALL_ERROR_INVALID_STATE;
        }

        process_id_t pid =
            process_id(
                task->process);

        (void)pid;

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

        if (tid == 0)
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
            task->process == NULL)
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
         * Validate a non-NULL userspace status destination BEFORE
         * collecting anything.
         *
         * process_waitpid() consumes the parent's ownership
         * reference when it succeeds. We must therefore never
         * discover an invalid userspace destination afterward.
         *
         * copy_from_user + copy_to_user of the same value verifies
         * both readability and writability without changing the
         * caller-visible value.
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

        int status =
            0;

        if (!process_waitpid(
                task->process,
                child_pid,
                &status))
        {
            return 0;
        }

        /*
         * Destination was already validated above while the child
         * was still owned by the parent.
         */
        if (user_status != NULL)
        {
            if (!copy_to_user(
                    user_status,
                    &status,
                    sizeof(status)))
            {
                /*
                 * This should not normally become possible without
                 * concurrent address-space mutation, which the
                 * current userspace model does not support.
                 *
                 * The child has already been collected, so report
                 * the address failure rather than pretending the
                 * wait did not happen.
                 */
                return I386_SYSCALL_ERROR_BAD_ADDRESS;
            }
        }

        return 1;
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