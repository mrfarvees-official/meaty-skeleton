#ifndef KERNEL_ARCH_I386_H
#define KERNEL_ARCH_I386_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile(
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
        : "memory");
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    
    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
        : "memory");

    return value;
}

static inline uint8_t ata_inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile(
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port));

    return value;
}

static inline void ata_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile(
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port));
}

static inline uint16_t ata_inw(uint16_t port)
{
    uint16_t value;

    __asm__ volatile(
        "inw  %1, %0"
        : "=a"(value)
        : "Nd"(port));

    return value;
}

static inline void io_wait(void)
{
    /*
     * Port 0x80 has historically been used for POST/debug.
     * A write here is commonly used as a tiny I/O delay.
     */
    outb(0x80, 0);
}

#endif