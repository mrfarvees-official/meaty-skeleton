.section .text
.code32

.extern interrupt_dispatch
.global isr_common

isr_common:
    cld
    pusha

    push %ds
    push %es
    push %fs
    push %gs

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es

    pushl %esp
    call interrupt_dispatch
    addl $4, %esp

    pop %gs
    pop %fs
    pop %es
    pop %ds

    popa
    addl $8, %esp
    iret