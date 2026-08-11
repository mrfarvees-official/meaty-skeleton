#ifndef KERNEL_BLOCK_DEVICE_H
#define KERNEL_BLOCK_DEVICE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct block_device block_device_t;

typedef int (*block_read_fn)(block_device_t *device, uint64_t lba, uint32_t count, void *buffer);

typedef int (*block_write_fn)(block_device_t *device, uint64_t lba, uint32_t count, const void *buffer);

struct block_device
{
    const char      *name;
    uint32_t        sector_size;
    uint64_t        sector_count;
    block_read_fn   read;
    block_write_fn  write;
    void            *private_data;
};

int block_read(block_device_t *device, uint64_t lba, uint32_t count, void *buffer);

int block_write(block_device_t *device, uint64_t lba, uint32_t count, const void *buffer);

bool block_device_valid(const block_device_t *device);

#endif