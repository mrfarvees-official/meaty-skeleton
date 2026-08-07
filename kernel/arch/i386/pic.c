#include <stdbool.h>
#include <stdint.h>

#include "io.h"
#include "pic.h"

#define PIC1_COMMAND      0x20u
#define PIC1_DATA         0x21u

#define PIC2_COMMAND      0xA0u
#define PIC2_DATA         0xA1u

#define PIC_EOI           0x20u

#define PIC_ICW1_ICW4     0x01u
#define PIC_ICW1_INIT     0x10u

#define PIC_ICW4_8086     0x01u

#define PIC_OCW3_READ_ISR 0x0Bu

static uint8_t master_mask = 0xFFu;
static uint8_t slave_mask = 0xFFu;

static uint8_t pic_read_isr(uint16_t command_port)
{
    outb(command_port, PIC_OCW3_READ_ISR);

    return inb(command_port);
}

void pic_initialize(void)
{
    /*
     * Begin initialization sequence.
     */
    outb(PIC1_COMMAND, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();

    outb(PIC2_COMMAND, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();

    /*
     * ICW2:
     *
     * Master IRQs -> vectors 0x20-0x27.
     * Slave IRQs  -> vectors 0x28-0x2F.
     */
    outb(PIC1_DATA, PIC_MASTER_VECTOR_OFFSET);
    io_wait();

    outb(PIC2_DATA, PIC_SLAVE_VECTOR_OFFSET);
    io_wait();

    /*
     * ICW3.
     *
     * Slave PIC is connected to IRQ2 on master.
     */
    outb(PIC1_DATA, 1u << 2);
    io_wait();

    /*
     * Slave identity = IRQ2.
     */
    outb(PIC2_DATA, 2u);
    io_wait();

    /*
     * ICW4:
     * 8086/88 mode.
     */
    outb(PIC1_DATA, PIC_ICW4_8086);
    io_wait();

    outb(PIC2_DATA, PIC_ICW4_8086);
    io_wait();

    /*
     * Start conservatively.
     *
     * Nothing should generate interrupts until its driver has
     * installed a handler and explicitly unmasks its IRQ.
     */
    master_mask = 0xFFu;
    slave_mask = 0xFFu;

    outb(PIC1_DATA, master_mask);
    outb(PIC2_DATA, slave_mask);
}

void pic_mask(uint8_t irq)
{
    if (irq >= PIC_IRQ_COUNT)
        return;

    if (irq < 8u) 
    {
        master_mask |= (uint8_t)(1u << irq);

        outb(PIC1_DATA, master_mask);

        return;
    }

    uint8_t slave_irq = irq - 8u;

    slave_mask |= (uint8_t)(1u << slave_irq);

    outb(PIC2_DATA, slave_mask);
}

void pic_unmask(uint8_t irq)
{
    if (irq >= PIC_IRQ_COUNT)
        return;

    if (irq < 8u)
    {
        master_mask &= (uint8_t)~(1u << irq);

        outb(PIC1_DATA, master_mask);

        return;
    }

    uint8_t slave_irq = irq - 8u;

    slave_mask &= (uint8_t)~(1u << slave_irq);

    outb(PIC2_DATA, slave_mask);

    /*
     * Slave interrupts travel through master IRQ2.
     *
     * Therefore IRQ2 must also be unmasked.
     */
    master_mask &= (uint8_t)~(1u << 2);

    outb(PIC1_DATA, master_mask);
}

bool pic_is_masked(uint8_t irq)
{
    if (irq >= PIC_IRQ_COUNT)
        return true;
    
    if (irq < 8u)
        return (master_mask & (1u << irq)) != 0;

    return (slave_mask & (1u << (irq - 8u))) != 0;
}

void pic_send_master_eoi(void)
{
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= PIC_IRQ_COUNT)
        return;

    /*
     * IRQ8-IRQ15 come through the slave PIC.
     *
     * Acknowledge slave first, then master.
     */
    if (irq >= 8u)
        outb(PIC2_COMMAND, PIC_EOI);

    outb(PIC1_COMMAND, PIC_EOI);
}

bool pic_is_spurious(uint8_t irq)
{
    /*
     * Only IRQ7 and IRQ15 can be the classic spurious PIC IRQs.
     */
    if (irq == 7u)
    {
        uint8_t isr = pic_read_isr(PIC1_COMMAND);

        /*
         * If bit 7 isn't present in the ISR, the IRQ was spurious.
         */
        return (isr & (1u << 7)) == 0;
    }

    if (irq == 15u)
    {
        uint8_t isr = pic_read_isr(PIC2_COMMAND);

        return (isr & (1u << 7)) == 0;
    }

    return false;
}

