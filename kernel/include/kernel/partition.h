#ifndef KERNEL_PARTITION_H
#define KERNEL_PARTITION_H

#include <stdint.h>
#include <stdbool.h>

#include <kernel/block_device.h>

#define MBR_PARTITION_COUNT  4
#define GPT_HEADER_SIGNATURE 0x5452415020494645ULL
#define PARTITION_MAX_COUNT  128

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

typedef struct __attribute__((packed))
{
    uint64_t signature;
    uint32_t version;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;

    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;

    uint8_t  disk_guid[16];

    uint64_t partition_entries_lba;
    uint32_t partition_entry_count;
    uint32_t partition_entry_size;
    uint32_t partition_entries_crc32;
} gpt_header_t;

typedef struct __attribute__((packed))
{
    uint8_t  type_guid[16];
    uint8_t  unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name[36];
} gpt_partition_entry_t;

typedef struct partition_device
{
    block_device_t block;
    block_device_t *parent;
    uint64_t start_lba;
    uint64_t sector_count;
} partition_device_t;

bool partition_from_mbr(block_device_t *disk, unsigned index, partition_device_t *partition);

bool partition_from_gpt(block_device_t *disk, unsigned index, partition_device_t *partition);

uint32_t crc32_compute(const void *data, size_t length);

size_t partition_scan(block_device_t *disk, partition_device_t *partitions, size_t max_partitions);

#endif