#include <stdint.h>
#include <stdio.h>

#include "interrupts.h"
#include "syscall.h"

static uint32_t syscall_dispatch(
    uint32_t number,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t arg2,
    uint32_t arg3,
    uint32_t arg4)
{
    switch (number)
    {
    case I386_SYSCALL_TEST_SIMPLE:
        return I386_SYSCALL_TEST_RESULT;

    case I386_SYSCALL_TEST_ARGUMENTS:
        printf(
            "U2b: args "
            "ebx=0x%lx ecx=0x%lx edx=0x%lx "
            "esi=0x%lx edi=0x%lx\n",
            (unsigned long)arg0,
            (unsigned long)arg1,
            (unsigned long)arg2,
            (unsigned long)arg3,
            (unsigned long)arg4);

        if (arg0 != 0x11u ||
            arg1 != 0x22u ||
            arg2 != 0x33u ||
            arg3 != 0x44u ||
            arg4 != 0x55u)
        {
            printf("U2b: argument ABI FAILED\n");
            return UINT32_MAX;
        }

        printf("U2b: argument ABI confirmed\n");

        return I386_SYSCALL_ARGUMENT_RESULT;

    default:
        return UINT32_MAX;
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

    uint32_t result =
        syscall_dispatch(
            frame->eax,
            frame->ebx,
            frame->ecx,
            frame->edx,
            frame->esi,
            frame->edi);

    frame->eax =
        result;

    printf(
        "U2: syscall %lu returning EAX=0x%lx\n",
        (unsigned long)number,
        (unsigned long)frame->eax);
}

bool syscall_initialize(void)
{
    return interrupt_register_handler(
               I386_SYSCALL_VECTOR,
               syscall_handler) == 0;
}