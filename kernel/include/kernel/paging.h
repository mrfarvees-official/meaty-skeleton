#ifndef KERNEL_PAGING_H
#define KERNEL_PAGING_H

#include <stdbool.h>
#include <stdint.h>

#define PAGE_SIZE 4096u

#define PAGE_PRESENT 0x001u
#define PAGE_WRITABLE 0x002u
#define PAGE_USER 0x004u
#define PAGE_WRITE_THROUGH 0x008u
#define PAGE_CACHE_DISABLE 0x010u
#define PAGE_ACCESSED 0x020u
#define PAGE_DIRTY 0x040u
#define PAGE_GLOBAL 0x100u

#define PAGE_FRAME 0xFFFFF000u
#define PAGE_FLAGS 0x00000FFFu

/*
 * Flags that callers may specify when creating mappings.
 * PAGE_PRESENT is added automatically.
 */
#define PAGE_MAP_FLAGS    \
    (PAGE_WRITABLE |      \
     PAGE_USER |          \
     PAGE_WRITE_THROUGH | \
     PAGE_CACHE_DISABLE | \
     PAGE_GLOBAL)

void paging_initialize(void);

/*
 * Return the physical page-directory frame currently loaded in CR3.
 */
uintptr_t paging_current_directory(void);

/*
 * Return the canonical kernel page-directory physical frame created
 * by paging_initialize().
 *
 * Kernel threads use this address space.
 */
uintptr_t paging_kernel_directory(void);

/*
 * Construct a new address space containing the current kernel's
 * supervisor mappings but no PAGE_USER mappings.
 *
 * The returned value is a physical page-directory frame suitable
 * for CR3.
 */
bool paging_create_user_directory(
    uintptr_t *directory_physical);

/*
 * Load one page-directory physical frame into CR3.
 */
bool paging_switch_directory(
    uintptr_t directory_physical);

/*
 * Destroy an address space created by
 * paging_create_user_directory().
 *
 * Present PAGE_USER mappings are private to that user address space:
 * their mapped frames and page-table frames are released.
 *
 * Supervisor mappings are shared with the kernel address space and
 * are never released here.
 *
 * The directory being destroyed must not be the currently active
 * address space.
 */
void paging_destroy_user_directory(
    uintptr_t directory_physical);

bool paging_share_kernel_pde(
    uintptr_t directory_physical,
    uintptr_t virtual_address);

bool paging_map_page(
    uintptr_t virtual_address,
    uintptr_t physical_address,
    uint32_t flags);

/*
 * Create a mapping in a page directory other than the currently
 * active address space.
 *
 * U4a keeps this deliberately small: the caller must currently be
 * running in the canonical kernel address space.  The target
 * directory is installed temporarily while paging_map_page() performs
 * the normal mapping operation.
 */
bool paging_map_page_in_directory(
    uintptr_t directory_physical,
    uintptr_t virtual_address,
    uintptr_t physical_address,
    uint32_t flags);

/*
 * release_frame == true:
 *     Remove the mapping and return the physical frame to the PMM.
 *
 * release_frame == false:
 *     Remove only the virtual mapping.
 */
bool paging_unmap_page(
    uintptr_t virtual_address,
    bool release_frame);

/*
 * Returns true when the virtual address is mapped.
 * The physical address, including the page offset, is written to
 * physical_address.
 */
bool paging_get_physical_address(
    uintptr_t virtual_address,
    uintptr_t *physical_address);

bool paging_is_mapped(uintptr_t virtual_address);

/*
 * Return effective permissions for the mapping containing
 * virtual_address.
 *
 * PAGE_PRESENT, PAGE_USER and PAGE_WRITABLE are reported only when
 * both the PDE and PTE allow that access.
 */
bool paging_get_effective_flags(
    uintptr_t virtual_address,
    uint32_t *flags);

bool paging_identity_map_range(
    uintptr_t physical_address,
    size_t length,
    uint32_t flags);

#endif