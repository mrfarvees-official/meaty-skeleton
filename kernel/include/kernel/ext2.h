#ifndef KERNEL_EXT2_H
#define KERNEL_EXT2_H

#include <stdint.h>

#include <kernel/vfs.h>
#include <kernel/block_device.h>

#define EXT2_ROOT_INODE  2
#define EXT2_S_IFMT      0xF000
#define EXT2_S_IFDIR     0x4000
#define EXT2_S_IFREG     0x8000
#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
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

typedef struct __attribute__((packed))
{
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;

    uint16_t free_blocks_count;
    uint16_t free_inodes_count;
    uint16_t used_dirs_count;
    uint16_t pad;

    uint8_t  reserved[12];
} ext2_block_group_descriptor_t;

typedef struct __attribute__((packed))
{
    uint16_t mode;
    uint16_t uid;

    uint32_t size_low;
    uint32_t access_time;
    uint32_t creation_time;
    uint32_t modification_time;
    uint32_t deletion_time;

    uint16_t gid;
    uint16_t link_count;

    uint32_t sector_count;
    uint32_t flags;
    uint32_t os_specific1;

    uint32_t block[15];

    uint32_t generation;
    uint32_t file_ac1;
    uint32_t size_high;
    uint32_t fragment_address;

    uint8_t os_specific2[12];
} ext2_inode_t;

typedef struct __attribute__((packed))
{
    uint32_t inode;
    uint16_t record_length;
    uint8_t  name_length;
    uint8_t  file_type;

    char name[];
} ext2_directory_entry_t;

typedef struct 
{
    block_device_t    *device;
    ext2_superblock_t superblock;
    vnode_t           root_vnode;
} ext2_fs_t;

typedef struct ext2_block_cache ext2_block_cache_t;

typedef struct 
{
    ext2_fs_t           *fs;
    uint32_t            inode_number;
    ext2_inode_t        inode;
    ext2_block_cache_t  *block_cache;
} ext2_vnode_data_t;

bool ext2_read_superblock(block_device_t *device, ext2_superblock_t *superblock);
bool ext2_read_group_descriptor(block_device_t *device, const ext2_superblock_t *superblock, uint32_t group, ext2_block_group_descriptor_t *descriptor);
bool ext2_read_inode(block_device_t *device, const ext2_superblock_t *superblock, uint32_t inode_number, ext2_inode_t *inode);
bool ext2_list_directory(block_device_t *device, const ext2_superblock_t *superblock, const ext2_inode_t *directory);
bool ext2_lookup(block_device_t *device, const ext2_superblock_t *superblock, const ext2_inode_t *directory, const char *name, uint32_t *inode_number);
bool ext2_read_file(block_device_t *device, const ext2_superblock_t *superblock, const ext2_inode_t *inode, size_t offset, void *buffer, size_t buffer_size, size_t *bytes_read);
bool ext2_mount(block_device_t *device, ext2_fs_t *fs);

#endif