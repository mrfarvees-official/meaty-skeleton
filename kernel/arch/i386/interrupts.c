#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "pic.h"
#include "interrupts.h"

#include <kernel/scheduler.h>

static interrupt_handler_t interrupt_handlers[INTERRUPT_VECTOR_COUNT];

static const char *const exception_names[32] =
    {
        "Divide Error",                   /* 0  */
        "Debug",                          /* 1  */
        "Non-maskable Interrupt",         /* 2  */
        "Breakpoint",                     /* 3  */
        "Overflow",                       /* 4  */
        "BOUND Range Exceeded",           /* 5  */
        "Invalid Opcode",                 /* 6  */
        "Device Not Available",           /* 7  */
        "Double Fault",                   /* 8  */
        "Coprocessor Segment Overrun",    /* 9, obsolete */
        "Invalid TSS",                    /* 10 */
        "Segment Not Present",            /* 11 */
        "Stack-Segment Fault",            /* 12 */
        "General Protection Fault",       /* 13 */
        "Page Fault",                     /* 14 */
        "Reserved",                       /* 15 */
        "x87 Floating-Point Exception",   /* 16 */
        "Alignment Check",                /* 17 */
        "Machine Check",                  /* 18 */
        "SIMD Floating-Point Exception",  /* 19 */
        "Virtualization Exception",       /* 20 */
        "Control Protection Exception",   /* 21 */
        "Reserved",                       /* 22 */
        "Reserved",                       /* 23 */
        "Reserved",                       /* 24 */
        "Reserved",                       /* 25 */
        "Reserved",                       /* 26 */
        "Reserved",                       /* 27 */
        "Hypervisor Injection Exception", /* 28 */
        "VMM Communication Exception",    /* 29 */
        "Security Exception",             /* 30 */
        "Reserved"                        /* 31 */
};

static __attribute__((noreturn)) void interrupt_halt(void)
{
    for (;;)
        __asm__ volatile("cli; hlt");
}

static uint32_t read_cr2(void)
{
    uint32_t value;

    __asm__ volatile(
        "movl %%cr2, %0"
        : "=r"(value));

    return value;
}

static void print_page_fault_error(uint32_t error_code)
{
    printf(
        "    access: %s\n",
        (error_code & (1u << 1)) ? "write" : "read");

    printf(
        "    cause: %s\n",
        (error_code & (1u << 0))
            ? "protection violation"
            : "non-present page");

    printf(
        "    privilege: %s\n",
        (error_code & (1u << 2))
            ? "user mode"
            : "supervisor mode");

    if ((error_code & (1u << 3)) != 0)
        printf("    reserved page-table bit was set\n");

    if ((error_code & (1u << 4)) != 0)
        printf("    occurred during instruction fetch\n");
}

static void print_registers(const struct interrupt_frame *frame)
{
    printf(
        "EAX=%lx EBX=%lx ECX=%lx EDX=%lx\n",
        (unsigned long)frame->eax,
        (unsigned long)frame->ebx,
        (unsigned long)frame->ecx,
        (unsigned long)frame->edx);

    printf(
        "ESI=%lx EDI=%lx EBP=%lx\n",
        (unsigned long)frame->esi,
        (unsigned long)frame->edi,
        (unsigned long)frame->ebp);
}

static __attribute__((noreturn)) void default_exception_handler(struct interrupt_frame *frame)
{
    const char *name = "Unknown exception";

    if (frame->vector < 32u)
        name = exception_names[frame->vector];

    printf("\n=== CPU EXCEPTION ===\n");
    printf(
        "Vector    : %lu (%s)\n",
        (unsigned long)frame->vector,
        name);

    printf(
        "Error code: 0x%lx\n",
        (unsigned long)frame->error_code);

    printf(
        "EIP       : 0x%lx\n",
        (unsigned long)frame->eip);

    printf(
        "CS        : 0x%lx\n",
        (unsigned long)frame->cs);

    printf(
        "EFLAGS    : 0x%lx\n",
        (unsigned long)frame->eflags);

    print_registers(frame);

    /*
     * user_esp and user_ss are pushed only when the CPU changes
     * privilege level while entering the interrupt.
     */
    if ((frame->cs & 3u) != 0)
    {
        printf(
            "User ESP  : 0x%lx\n",
            (unsigned long)frame->user_esp);

        printf(
            "User SS   : 0x%lx\n",
            (unsigned long)frame->user_ss);
    }

    interrupt_halt();
}

static __attribute__((noreturn)) void default_page_fault_handler(struct interrupt_frame *frame)
{
    uint32_t fault_address = read_cr2();

    printf("\n=== PAGE FAULT ===\n");

    printf(
        "Fault address : 0x%lx\n",
        (unsigned long)fault_address);

    printf(
        "Instruction   : 0x%lx\n",
        (unsigned long)frame->eip);

    printf(
        "Error code    : 0x%lx\n",
        (unsigned long)frame->error_code);

    print_page_fault_error(frame->error_code);
    print_registers(frame);

    interrupt_halt();
}

static void breakpoint_handler(struct interrupt_frame *frame)
{
    if ((frame->cs & 3u) == 3u)
    {
        printf("\n=== U1 USER-MODE TRAP ===\n");

        printf(
            "Vector    : %lu\n",
            (unsigned long)frame->vector);

        printf(
            "EIP       : 0x%lx\n",
            (unsigned long)frame->eip);

        printf(
            "CS        : 0x%lx\n",
            (unsigned long)frame->cs);

        printf(
            "CS RPL    : %lu\n",
            (unsigned long)(frame->cs & 3u));

        printf(
            "User ESP  : 0x%lx\n",
            (unsigned long)frame->user_esp);

        printf(
            "User SS   : 0x%lx\n",
            (unsigned long)frame->user_ss);

        printf("U1: CPL=3 trap confirmed\n");

        /*
         * This milestone intentionally stops here.
         *
         * We do not yet have a real user task lifecycle or syscall
         * return path.
         */
        interrupt_halt();
    }

    printf(
        "Breakpoint at EIP=0x%lx\n",
        (unsigned long)frame->eip);

    /*
     * Kernel-mode INT3 remains recoverable as before.
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

void interrupt_dispatch(struct interrupt_frame *frame)
{
    if (frame == NULL)
        interrupt_halt();

    if (frame->vector >= INTERRUPT_VECTOR_COUNT)
    {
        printf(
            "Invalid interrupt vector %lu\n",
            (unsigned long)frame->vector);

        interrupt_halt();
    }

    /*
     * ----------------------------------------------------------
     * CPU exceptions: vectors 0-31
     * ----------------------------------------------------------
     */
    if (frame->vector < 32u)
    {
        interrupt_handler_t handler =
            interrupt_handlers[frame->vector];

        if (handler != NULL)
        {
            handler(frame);

            /*
             * Recoverable exceptions such as INT3 return here.
             *
             * Fatal handlers such as the default page-fault
             * handler never return.
             */
            return;
        }

        default_exception_handler(frame);
    }

    /*
     * ----------------------------------------------------------
     * Legacy 8259 PIC hardware IRQs: vectors 32-47
     * ----------------------------------------------------------
     */
    if (pic_vector_is_irq(frame->vector))
    {
        uint8_t irq =
            pic_vector_to_irq(frame->vector);

        /*
         * The classic 8259 can generate spurious IRQ7 / IRQ15.
         *
         * They require special acknowledgement behavior.
         */
        if ((irq == 7u || irq == 15u) &&
            pic_is_spurious(irq))
        {
            /*
             * Spurious IRQ7:
             *
             * Do not send EOI because the master PIC does not
             * have IRQ7 marked in-service.
             *
             * Spurious IRQ15:
             *
             * The slave does not have IRQ15 in-service, but the
             * master accepted IRQ2 (the cascade line), so only
             * the master PIC requires EOI.
             */
            if (irq == 15u)
                pic_send_master_eoi();

            return;
        }

        interrupt_handler_t handler =
            interrupt_handlers[frame->vector];

        if (handler != NULL)
        {
            handler(frame);
        }

        /*
         * IRQ acknowledgement belongs to the generic interrupt
         * layer rather than individual device drivers.
         */
        pic_send_eoi(irq);

        /*
         * IRQ handlers are finished and the PIC has been acknowledged.
         *
         * If the timer or another IRQ requested a reschedule,
         * it is now safe to switch tasks.
         */
        scheduler_handle_safe_preemption_point(frame->eflags);

        return;
    }

    /*
     * ----------------------------------------------------------
     * Other vectors
     * ----------------------------------------------------------
     *
     * Reserved for things such as:
     *
     *   - software interrupts
     *   - syscalls
     *   - APIC vectors
     *   - MSI/MSI-X
     */
    interrupt_handler_t handler =
        interrupt_handlers[frame->vector];

    if (handler != NULL)
    {
        handler(frame);

        scheduler_handle_safe_preemption_point(frame->eflags);
        return;
    }

    printf(
        "Unhandled interrupt vector %lu\n",
        (unsigned long)frame->vector);

    interrupt_halt();
}

uint32_t interrupt_save_disable(void)
{
    uint32_t flags;

    __asm__ volatile(
        "pushfl\n"
        "popl %0\n"
        "cli"
        : "=r"(flags)
        :
        : "memory");

    return flags;
}

void interrupt_restore(uint32_t flags)
{
    /*
     * EFLAGS.IF is bit 9.
     */
    if ((flags & (1u << 9)) != 0)
    {
        __asm__ volatile(
            "sti"
            :
            :
            : "memory");
    }
    else
    {
        __asm__ volatile(
            "cli"
            :
            :
            : "memory");
    }
}

void interrupt_disable(void)
{
    __asm__ volatile(
        "cli"
        :
        :
        : "memory");
}

void interrupt_enable(void)
{
    __asm__ volatile(
        "sti"
        :
        :
        : "memory");
}
