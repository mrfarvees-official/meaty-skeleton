/*
 * AP startup page.
 *
 * The BSP copies this position-independent blob to physical 0x8000 and
 * sends its page number in a SIPI.  Every address used before paging is a
 * physical address in that page; after CR3/PG are installed the kernel's
 * identity mapping keeps the page and the kernel executable.
 */
.set AP_TRAMPOLINE_PHYSICAL, 0x8000

.section .ap_trampoline, "ax"
.code16

.global ap_trampoline_start
ap_trampoline_start:
    cli

    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    lgdt ap_trampoline_gdt_descriptor - ap_trampoline_start + AP_TRAMPOLINE_PHYSICAL

    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0

    ljmpl $0x08, $ap_trampoline_protected - ap_trampoline_start + AP_TRAMPOLINE_PHYSICAL

.code32
ap_trampoline_protected:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    movl ap_trampoline_stack - ap_trampoline_start + AP_TRAMPOLINE_PHYSICAL, %esp

    movl ap_trampoline_cr3 - ap_trampoline_start + AP_TRAMPOLINE_PHYSICAL, %eax
    movl %eax, %cr3
    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    call *ap_trampoline_entry - ap_trampoline_start + AP_TRAMPOLINE_PHYSICAL

1:
    cli
    hlt
    jmp 1b

.align 8
ap_trampoline_gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
ap_trampoline_gdt_end:

ap_trampoline_gdt_descriptor:
    .word ap_trampoline_gdt_end - ap_trampoline_gdt - 1
    .long AP_TRAMPOLINE_PHYSICAL + (ap_trampoline_gdt - ap_trampoline_start)

.align 4
.global ap_trampoline_stack
ap_trampoline_stack:
    .long 0
.global ap_trampoline_cr3
ap_trampoline_cr3:
    .long 0
.global ap_trampoline_entry
ap_trampoline_entry:
    .long 0

.global ap_trampoline_end
ap_trampoline_end:
