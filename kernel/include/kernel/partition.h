#ifndef KERNEL_PARTITION_H
#define KERNEL_PARTITION_H

#include <stdint.h>
#include <stdbool.h>

#include <kernel/block_device.h>

#define MBR_PARTITION_COUNT 4

typedef struct __attribute__((packed))
{
    uint8_t  boot_indicator;

    uint8_t  start_head;
    uint8_t  start_sector;
    uint8_t  start_cylinder;

    uint8_t  type;

    uint8_t  end_head;
    uint8_t  end_sector;
    uint8_t  end_cylinder;

    uint32_t start_lba;
    uint32_t sector_count;
} mbr_partition_entry_t;

typedef struct partition_device
{
    block_device_t block;
    block_device_t *parent;
    uint64_t start_lba;
    uint64_t sector_count;
} partition_device_t;

bool partition_from_mbr(block_device_t *disk, unsigned index, partition_device_t *partition);

#endif