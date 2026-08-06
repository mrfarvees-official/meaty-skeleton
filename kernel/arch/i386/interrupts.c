#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "interrupts.h"

static interrupt_handler_t interrupt_handlers[INTERRUPT_VECTOR_COUNT];

static const char* const exception_names[32] =
{
    "Divide Error",                    /* 0  */
    "Debug",                           /* 1  */
    "Non-maskable Interrupt",          /* 2  */
    "Breakpoint",                      /* 3  */
    "Overflow",                        /* 4  */
    "BOUND Range Exceeded",            /* 5  */
    "Invalid Opcode",                  /* 6  */
    "Device Not Available",            /* 7  */
    "Double Fault",                    /* 8  */
    "Coprocessor Segment Overrun",     /* 9, obsolete */
    "Invalid TSS",                     /* 10 */
    "Segment Not Present",             /* 11 */
    "Stack-Segment Fault",             /* 12 */
    "General Protection Fault",        /* 13 */
    "Page Fault",                      /* 14 */
    "Reserved",                        /* 15 */
    "x87 Floating-Point Exception",    /* 16 */
    "Alignment Check",                 /* 17 */
    "Machine Check",                   /* 18 */
    "SIMD Floating-Point Exception",   /* 19 */
    "Virtualization Exception",        /* 20 */
    "Control Protection Exception",    /* 21 */
    "Reserved",                        /* 22 */
    "Reserved",                        /* 23 */
    "Reserved",                        /* 24 */
    "Reserved",                        /* 25 */
    "Reserved",                        /* 26 */
    "Reserved",                        /* 27 */
    "Hypervisor Injection Exception",  /* 28 */
    "VMM Communication Exception",     /* 29 */
    "Security Exception",              /* 30 */
    "Reserved"                         /* 31 */
};

static __attribute__((noreturn)) void interrupt_halt(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

static uint32_t read_cr2(void)
{
    uint32_t value;

    __asm__ volatile (
        "movl %%cr2, %0"
        : "=r"(value)
    );

    return value;
}

static void print_page_fault_error(uint32_t error_code)
{
    printf(
        "    access: %s\n",
        (error_code & (1u << 1)) ? "write" : "read"
    );

    printf(
        "    cause: %s\n",
        (error_code & (1u << 0))
            ? "protection violation"
            : "non-present page"
    );

    printf(
        "    privilege: %s\n",
        (error_code & (1u << 2))
            ? "user mode"
            : "supervisor mode"
    );

    if ((error_code & (1u << 3)) != 0)
        printf("    reserved page-table bit was set\n");

    if ((error_code & (1u << 4)) != 0)
        printf("    occurred during instruction fetch\n");
}

static void print_registers(const struct interrupt_frame* frame)
{
    printf(
        "EAX=%lx EBX=%lx ECX=%lx EDX=%lx\n",
        (unsigned long)frame->eax,
        (unsigned long)frame->ebx,
        (unsigned long)frame->ecx,
        (unsigned long)frame->edx
    );

    printf(
        "ESI=%lx EDI=%lx EBP=%lx\n",
        (unsigned long)frame->esi,
        (unsigned long)frame->edi,
        (unsigned long)frame->ebp
    );
}

static __attribute__((noreturn))
void default_exception_handler(struct interrupt_frame* frame)
{
    const char* name = "Unknown exception";

    if (frame->vector < 32u)
        name = exception_names[frame->vector];

    printf("\n=== CPU EXCEPTION ===\n");
    printf(
        "Vector    : %lu (%s)\n",
        (unsigned long)frame->vector,
        name
    );

    printf(
        "Error code: 0x%lx\n",
        (unsigned long)frame->error_code
    );

    printf(
        "EIP       : 0x%lx\n",
        (unsigned long)frame->eip
    );

    printf(
        "CS        : 0x%lx\n",
        (unsigned long)frame->cs
    );

    printf(
        "EFLAGS    : 0x%lx\n",
        (unsigned long)frame->eflags
    );

    print_registers(frame);

    /*
     * user_esp and user_ss are pushed only when the CPU changes
     * privilege level while entering the interrupt.
     */
    if ((frame->cs & 3u) != 0)
    {
        printf(
            "User ESP  : 0x%lx\n",
            (unsigned long)frame->user_esp
        );

        printf(
            "User SS   : 0x%lx\n",
            (unsigned long)frame->user_ss
        );
    }

    interrupt_halt();
}

static __attribute__((noreturn))
void default_page_fault_handler(struct interrupt_frame* frame)
{
    uint32_t fault_address = read_cr2();

    printf("\n=== PAGE FAULT ===\n");

    printf(
        "Fault address : 0x%lx\n",
        (unsigned long)fault_address
    );

    printf(
        "Instruction   : 0x%lx\n",
        (unsigned long)frame->eip
    );

    printf(
        "Error code    : 0x%lx\n",
        (unsigned long)frame->error_code
    );

    print_page_fault_error(frame->error_code);
    print_registers(frame);

    interrupt_halt();
}

static void breakpoint_handler(struct interrupt_frame* frame)
{
    printf(
        "Breakpoint at EIP=0x%lx\n",
        (unsigned long)frame->eip
    );

    /*
     * Returning resumes execution after the INT3 instruction.
     */
}

void interrupt_initialization(void)
{
    /*
     * This loop is technically unnecessary because static storage is
     * zero-initialized, but keeping it is harmless and explicit.
     */
    for (size_t i = 0; i < INTERRUPT_VECTOR_COUNT; ++i)
        interrupt_handlers[i] = NULL;

    interrupt_register_handler(3, breakpoint_handler);
    interrupt_register_handler(14, default_page_fault_handler);
}

int interrupt_register_handler(
    uint8_t vector,
    interrupt_handler_t handler)
{
    if (handler == NULL)
        return -1;

    interrupt_handlers[vector] = handler;

    return 0;
}

void interrupt_unregister_handler(uint8_t vector)
{
    interrupt_handlers[vector] = NULL;
}

void interrupt_dispatch(struct interrupt_frame* frame)
{
    if (frame == NULL)
        interrupt_halt();

    /*
     * This check matters only if vector is wider than uint8_t in your
     * interrupt frame structure.
     */
    if (frame->vector >= INTERRUPT_VECTOR_COUNT)
    {
        printf(
            "Invalid interrupt vector %lu\n",
            (unsigned long)frame->vector
        );

        interrupt_halt();
    }

    interrupt_handler_t handler =
        interrupt_handlers[frame->vector];

    if (handler != NULL)
    {
        handler(frame);

        /*
         * Required for recoverable handlers such as INT3.
         *
         * The page-fault handler never reaches here because it halts.
         */
        return;
    }

    if (frame->vector < 32u)
        default_exception_handler(frame);

    /*
     * Later, IRQ and syscall dispatching should happen before the
     * final unhandled-interrupt path.
     */

    printf(
        "Unhandled interrupt vector %lu\n",
        (unsigned long)frame->vector
    );

    interrupt_halt();
}