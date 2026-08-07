.section .text
.code32

.extern interrupt_dispatch
.global isr_common

isr_common:
    cld

    /*
     * Save general-purpose state.
     *
     * PUSHA pushes:
     *
     * eax
     * ecx
     * edx
     * ebx
     * original esp
     * ebp
     * esi
     * edi
     *
     * Because the stack grows downward, ESP finally points at EDI.
     */
    pusha

    /*
     * Preserve interrupted segment selectors.
     */
    push %ds
    push %es
    push %fs
    push %gs

    /*
     * C kernel code expects kernel data segments.
     */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es

    /*
     * ESP now points directly to struct interrupt_frame.
     */
    pushl %esp
    call interrupt_dispatch
    addl $4, %esp

    /*
     * Restore exactly what was interrupted.
     */
    pop %gs
    pop %fs
    pop %es
    pop %ds

    popa

    /*
     * Drop:
     *
     * vector
     * error_code
     */
    addl $8, %esp

    iret