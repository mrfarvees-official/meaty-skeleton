#include <stdint.h>
#include <stdio.h>

#include <kernel/multiboot.h>
#include <kernel/tty.h>

void print_memory_map(const struct multiboot_info* mbi) 
{
    if ((mbi->flags & MULTIBOOT_INFO_MEMORY_MAP) == 0)
    {
        printf("No Multiboot memory map\n");
        return;
    }

    uintptr_t current = mbi->mmap_addr;
    uintptr_t end = mbi->mmap_addr + mbi->mmap_lengh;

    while (current < end) 
    {
        const struct multiboot_mmap_entry* entry = (const struct multiboot_mmap_entry*)current;

        /**
         * Our printf supports 64-bit values, 
         * but let's print the lower 32 bits initially.
         */
        printf("base=%x length=%x type=%u\n", (uint32_t)entry->address, (uint32_t)entry->length, entry->type);

        current += entry->size + sizeof(entry->size);
    }

}