.section .text
.code32

.global gdt_flush
.type gdt_flush, @function

/*
 * void gdt_flush(const struct gdt_pointer* gdtr);
 *
 * 32-bit cdecl calling convention:
 *   4(%esp) = address of the GDTR structure
 *
 * struct gdt_pointer:
 *   uint16_t limit;
 *   uint32_t base;
 * } __attribute__((packed));
 */
gdt_flush:
    /* Load the new Global Descriptor Table. */
    movl 4(%esp), %eax
    lgdt (%eax)

    /* Reload all data segment registers using selector 0x10. */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /*
     * Reload CS using a far return.
     *
     * lret pops:
     *   1. EIP
     *   2. CS
     *
     * Therefore, push CS first and the new EIP second.
     */
    pushl $0x08
    pushl $gdt_flush_done
    lret

gdt_flush_done:
    ret

.size gdt_flush, . - gdt_flush
