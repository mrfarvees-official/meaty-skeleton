.section .text
.code32

.extern isr_common

.macro ISR_NO_ERROR vector
.global isr_\vector
isr_\vector:
    pushl $0
    pushl $\vector
    jmp isr_common
.endm

.macro ISR_ERROR vector
.global isr_\vector
isr_\vector:
    pushl $\vector
    jmp isr_common
.endm

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_ERROR    8
ISR_NO_ERROR 9
ISR_ERROR    10
ISR_ERROR    11
ISR_ERROR    12
ISR_ERROR    13
ISR_ERROR    14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_ERROR    17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_NO_ERROR 21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_NO_ERROR 29
ISR_NO_ERROR 30
ISR_NO_ERROR 31

/*
 * Software syscall entry.
 *
 * Software INT does not supply an error code, so match the normal
 * no-error interrupt-frame layout expected by isr_common.
 */
ISR_NO_ERROR 128

.global isr_apic_timer
isr_apic_timer:
    pushl $0
    pushl $0xF0
    jmp isr_common

.global isr_apic_reschedule
isr_apic_reschedule:
    pushl $0
    pushl $0xF1
    jmp isr_common
