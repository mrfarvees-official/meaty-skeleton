#include <stdint.h>
#include <stdio.h>

#include <kernel/task.h>
#include <kernel/usercopy.h>

#include "interrupts.h"
#include "syscall.h"

static int32_t syscall_dispatch(
    uint32_t number,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t arg2,
    uint32_t arg3,
    uint32_t arg4)
{

    switch (number)
    {
    case I386_SYSCALL_GETTID:
    {
        task_t *task =
            task_current();

        if (task == NULL)
            return I386_SYSCALL_ERROR_INVALID_STATE;

        printf(
            "U2c: gettid -> %lu\n",
            (unsigned long)task->id);

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

        /*
         * U2d intentionally keeps the probe bounded.
         */
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

    printf(
        "U2: syscall entry number=%lu CS=0x%lx RPL=%lu\n",
        (unsigned long)number,
        (unsigned long)frame->cs,
        (unsigned long)(frame->cs & 3u));

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

    printf(
        "U2: syscall %lu returning %ld\n",
        (unsigned long)number,
        (long)result);
}

bool syscall_initialize(void)
{
    return interrupt_register_handler(
               I386_SYSCALL_VECTOR,
               syscall_handler) == 0;
}