#ifndef KERNEL_PAGING_H
#define KERNEL_PAGING_H

#include <stdbool.h>
#include <stdint.h>


#define PAGE_SIZE          4096u

#define PAGE_PRESENT       0x001u
#define PAGE_WRITABLE      0x002u
#define PAGE_USER          0x004u
#define PAGE_WRITE_THROUGH 0x008u
#define PAGE_CACHE_DISABLE 0x010u
#define PAGE_ACCESSED      0x020u
#define PAGE_DIRTY         0x040u
#define PAGE_GLOBAL        0x100u

#define PAGE_FRAME         0xFFFFF000u
#define PAGE_FLAGS         0x00000FFFu

/*
 * Flags that callers may specify when creating mappings.
 * PAGE_PRESENT is added automatically.
 */
#define PAGE_MAP_FLAGS       \
    (PAGE_WRITABLE         | \
     PAGE_USER             | \
     PAGE_WRITE_THROUGH    | \
     PAGE_CACHE_DISABLE    | \
     PAGE_GLOBAL)

void paging_initialize(void);

bool paging_map_page(
    uintptr_t virtual_address,
    uintptr_t physical_address,
    uint32_t flags
);

/*
 * release_frame == true:
 *     Remove the mapping and return the physical frame to the PMM.
 *
 * release_frame == false:
 *     Remove only the virtual mapping.
 */
bool paging_unmap_page(
    uintptr_t virtual_address,
    bool release_frame
);

/*
 * Returns true when the virtual address is mapped.
 * The physical address, including the page offset, is written to
 * physical_address.
 */
bool paging_get_physical_address(
    uintptr_t virtual_address,
    uintptr_t* physical_address
);

bool paging_is_mapped(uintptr_t virtual_address);

#endif