#ifndef KERNEL_EXT2_H
#define KERNEL_EXT2_H

#include <stdint.h>

#include <kernel/block_device.h>

typedef struct __attribute__((packed))
{
    uint32_t inode_count;
    uint32_t block_count;
    uint32_t reserved_block_count;
    uint32_t free_block_count;
    uint32_t free_inode_count;

    uint32_t first_data_block;
    uint32_t log_block_size;
    int32_t  log_fragment_size;

    uint32_t blocks_per_group;
    uint32_t fragments_per_group;
    uint32_t inodes_per_group;

    uint32_t mount_time;
    uint32_t write_time;

    uint16_t mount_count;
    uint16_t max_mount_count;

    uint16_t magic;
    uint16_t state;
    uint16_t errors;
    uint16_t minor_revision;

    uint32_t last_check;
    uint32_t check_interval;
    uint32_t creator_os;
    uint32_t revision_level;

    uint16_t default_uid;
    uint16_t default_gid;

    /*
     * Extended superblock fields start here for revision >= 1
     */
    uint32_t first_non_reserved_inode;
    uint16_t inode_size;
    uint16_t block_group_number;
} ext2_superblock_t;

bool ext2_read_superblock(block_device_t *device, ext2_superblock_t *superblock);

#endif