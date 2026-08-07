#ifndef KERNEL_HEAP_H
#define KERNEL_HEAP_H

#include <stddef.h>

typedef struct __attribute__((aligned(16))) heap_block
{
    size_t size;

    struct heap_block* previous;
    struct heap_block* next;

    uint32_t magic;
    bool free;
} heap_block_t;

_Static_assert(
    sizeof(heap_block_t) % 16 == 0,
    "heap_block_t must be multiple of 16 bytes"
);

void heap_initialize(void);

void* kmalloc(size_t size);
void* kcalloc(size_t count, size_t size);
void* krealloc(void* pointer, size_t new_size);
void  kfree(void* pointer);

#endif