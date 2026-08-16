#include <stdint.h>
#include <stdio.h>

#include "interrupts.h"
#include "syscall.h"

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

        frame->eax = UINT32_MAX;
        return;
    }

    uint32_t number =
        frame->eax;

    printf(
        "U2: syscall entry number=%lu CS=0x%lx RPL=%lu\n",
        (unsigned long)number,
        (unsigned long)frame->cs,
        (unsigned long)(frame->cs & 3u));

    switch (number)
    {
        case I386_SYSCALL_TEST:
            /*
             * Writing saved EAX is enough:
             * isr_common restores it before IRET.
             */
            frame->eax =
                I386_SYSCALL_TEST_RESULT;

            printf(
                "U2: returning EAX=0x%lx\n",
                (unsigned long)frame->eax);

            break;

        default:
            frame->eax =
                UINT32_MAX;

            printf(
                "U2: unknown syscall %lu\n",
                (unsigned long)number);

            break;
    }
}

bool syscall_initialize(void)
{
    return
        interrupt_register_handler(
            I386_SYSCALL_VECTOR,
            syscall_handler) == 0;
}