#ifndef KERNEL_MULTIBOOT_H
#define KERNEL_MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u
#define MULTIBOOT_INFO_MEMORY      (1u << 0);
#define MULTIBOOT_INFO_MEMORY_MAP  (1u << 6)
#define MULTIBOOT_MEMORY_AVAILABLE 1u

struct multiboot_info
{
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_lengh;
    uint32_t mmap_addr;
} __attribute__((packed));

struct multiboot_mmap_entry
{
    uint32_t size;
    uint64_t address;
    uint64_t length;
    uint32_t type;
} __attribute__((packed));

void print_memory_map(const struct multiboot_info* mbi);

#endif