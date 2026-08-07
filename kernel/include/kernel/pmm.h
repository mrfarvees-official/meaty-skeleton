#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096u

void pmm_initialize(uint32_t multiboot_info_address);

uintptr_t pmm_allocate_frame(void);
void pmm_free_frame(uintptr_t address);

void pmm_reserve_range(uintptr_t base, size_t length);
void pmm_release_range(uintptr_t base, size_t length);

size_t pmm_get_free_frame_count(void);
uint64_t pmm_get_usable_memory(void);
uint64_t pmm_get_free_memory(void);

#endif