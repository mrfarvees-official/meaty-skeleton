.section .rodata
.align 4

.global isr_stub_table

isr_stub_table:
    .long isr_0
    .long isr_1
    .long isr_2
    .long isr_3
    .long isr_4
    .long isr_5
    .long isr_6
    .long isr_7
    .long isr_8
    .long isr_9
    .long isr_10
    .long isr_11
    .long isr_12
    .long isr_13
    .long isr_14
    .long isr_15
    .long isr_16
    .long isr_17
    .long isr_18
    .long isr_19
    .long isr_20
    .long isr_21
    .long isr_22
    .long isr_23
    .long isr_24
    .long isr_25
    .long isr_26
    .long isr_27
    .long isr_28
    .long isr_29
    .long isr_30
    .long isr_31

.align 4

.global irq_stub_table

irq_stub_table:
    .long irq_0
    .long irq_1
    .long irq_2
    .long irq_3
    .long irq_4
    .long irq_5
    .long irq_6
    .long irq_7
    .long irq_8
    .long irq_9
    .long irq_10
    .long irq_11
    .long irq_12
    .long irq_13
    .long irq_14
    .long irq_15


.section .text

.extern isr_common


.macro IRQ_STUB irq vector

.global irq_\irq
.type irq_\irq, @function

irq_\irq:
    pushl $0
    pushl $\vector
    jmp isr_common

.endm


IRQ_STUB 0,  32
IRQ_STUB 1,  33
IRQ_STUB 2,  34
IRQ_STUB 3,  35
IRQ_STUB 4,  36
IRQ_STUB 5,  37
IRQ_STUB 6,  38
IRQ_STUB 7,  39

IRQ_STUB 8,  40
IRQ_STUB 9,  41
IRQ_STUB 10, 42
IRQ_STUB 11, 43
IRQ_STUB 12, 44
IRQ_STUB 13, 45
IRQ_STUB 14, 46
IRQ_STUB 15, 47