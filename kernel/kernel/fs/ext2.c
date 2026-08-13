#include <stdio.h>
#include <string.h>

#include <kernel/ext2.h>
#include <kernel/block_device.h>
#include <kernel/heap.h>
#include <kernel/vfs.h>

struct ext2_block_cache
{
    bool single_valid;
    uint32_t single_block_number;
    uint32_t single_entries[4096 / sizeof(uint32_t)];

    bool double_root_valid;
    uint32_t double_root_block_number;
    uint32_t double_root_entries[4096 / sizeof(uint32_t)];

    bool double_leaf_valid;
    uint32_t double_leaf_block_number;
    uint32_t double_leaf_entries[4096 / sizeof(uint32_t)];
};

static int ext2_vnode_lookup(vnode_t *directory, const char *name, vnode_t **result);
static int ext2_vnode_read(vnode_t *node, size_t offset, void *buffer, size_t size, size_t *bytes_read);
static bool ext2_get_data_block(
    block_device_t *device,
    const ext2_superblock_t *superblock,
    const ext2_inode_t *inode,
    uint32_t logical_block,
    uint32_t *physical_block,
    ext2_block_cache_t *cache);
static bool ext2_read_file_cached(
    block_device_t *device,
    const ext2_superblock_t *superblock,
    const ext2_inode_t *inode, size_t offset,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_read,
    ext2_block_cache_t *cache);

static const vnode_ops_t ext2_directory_ops;
static const vnode_ops_t ext2_file_ops;

static const vnode_ops_t ext2_directory_ops =
    {
        .lookup = ext2_vnode_lookup,
        .read = NULL,
        .write = NULL};

static const vnode_ops_t ext2_file_ops =
    {
        .lookup = NULL,
        .read = ext2_vnode_read,
        .write = NULL};

bool ext2_read_superblock(block_device_t *device, ext2_superblock_t *superblock)
{
    if (device == NULL || superblock == NULL)
        return false;

    if (device->sector_size != 512)
        return false;

    uint8_t buffer[1024];

    /*
     * ext2 superblock begins at byte offset 1024
     *
     * With 512-byte sectors:
     *
     * LBA 2 and LBA 3
     */
    if (block_read(device, 2, 2, buffer) != 0)
        return false;

    memcpy(superblock, buffer, sizeof(ext2_superblock_t));

    if (superblock->magic != 0xEF53)
        return false;

    return true;
}

static bool ext2_read_block(block_device_t *device, uint32_t block_size, uint32_t block_number, void *buffer)
{
    if (device == NULL || buffer == NULL)
        return false;

    if (block_size == 0)
        return false;

    if (block_size % device->sector_size != 0)
        return false;

    uint32_t sectors_per_block = block_size / device->sector_size;

    uint64_t lba = (uint64_t)block_number * sectors_per_block;

    return block_read(device, lba, sectors_per_block, buffer) == 0;
}

bool ext2_read_group_descriptor(
    block_device_t *device,
    const ext2_superblock_t *superblock,
    uint32_t group,
    ext2_block_group_descriptor_t *descriptor)
{
    if (device == NULL || superblock == NULL || descriptor == NULL)
        return false;

    uint32_t block_size = 1024u << superblock->log_block_size;
    uint32_t descriptors_per_block = block_size / sizeof(ext2_block_group_descriptor_t);

    if (descriptors_per_block == 0)
        return false;

    uint32_t bgdt_first_block = superblock->first_data_block + 1;
    uint32_t descriptor_block = bgdt_first_block + group / descriptors_per_block;
    uint32_t descriptor_index = group % descriptors_per_block;

    /*
     * For now limit test block size to <= 4096.
     */
    uint8_t block[4096];

    if (block_size > sizeof(block))
        return false;

    if (!ext2_read_block(device, block_size, descriptor_block, block))
        return false;

    const ext2_block_group_descriptor_t *entries = (const ext2_block_group_descriptor_t *)block;

    *descriptor = entries[descriptor_index];

    return true;
}

bool ext2_read_inode(block_device_t *device, const ext2_superblock_t *superblock, uint32_t inode_number, ext2_inode_t *inode)
{
    if (device == NULL || superblock == NULL || inode == NULL)
        return false;

    if (inode_number == 0 || inode_number > superblock->inode_count)
        return false;

    uint32_t block_size = 1024u << superblock->log_block_size;
    uint32_t inode_index = inode_number - 1;
    uint32_t group = inode_index / superblock->inodes_per_group;
    uint16_t index_in_group = inode_index % superblock->inodes_per_group;

    ext2_block_group_descriptor_t descriptor;

    if (!ext2_read_group_descriptor(device, superblock, group, &descriptor))
        return false;

    uint64_t byte_offset = (uint64_t)index_in_group * superblock->inode_size;
    uint32_t block_number = descriptor.inode_table + (uint32_t)(byte_offset / block_size);
    uint32_t offset_in_block = (uint32_t)(byte_offset % block_size);

    /*
     * The classic inode data we currently care about
     * is 128 bytes
     */
    if ((uint64_t)offset_in_block + sizeof(ext2_inode_t) > block_size)
        return false;

    uint8_t block[4096];

    if (block_size > sizeof(block))
        return false;

    if (!ext2_read_block(device, block_size, block_number, block))
        return false;

    memcpy(inode, block + offset_in_block, sizeof(ext2_inode_t));

    return true;
}

bool ext2_list_directory(block_device_t *device, const ext2_superblock_t *superblock, const ext2_inode_t *directory)
{
    if (device == NULL || superblock == NULL || directory == NULL)
        return false;

    if ((directory->mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
        return false;

    uint32_t block_size = 1024 << superblock->log_block_size;

    if (block_size > 4096)
        return false;

    uint8_t block[4096];

    /*
     * For now just parse the first direct block.
     */
    uint32_t block_number = directory->block[0];

    if (block_number == 0)
        return false;

    if (!ext2_read_block(device, block_size, block_number, block))
        return false;

    size_t offset = 0;

    while (offset < block_size)
    {
        ext2_directory_entry_t *entry = (ext2_directory_entry_t *)(block + offset);

        /*
         * Corruption / infinite-loop protection.
         */
        if (entry->record_length < 8)
            return false;

        if (offset + entry->record_length > block_size)
            return false;

        if (entry->name_length > entry->record_length - 8)
            return false;

        /*
         * inode = 0 means unused directory entry.
         */
        if (entry->inode != 0)
        {
            char name[256];

            size_t length = entry->name_length;

            if (length >= sizeof(name))
                return false;

            memcpy(name, entry->name, length);

            name[length] = '\0';

            printf("EXT2 dir: inode=%u type=%u name=%s\n", (unsigned)entry->inode, (unsigned)entry->file_type, name);
        }

        offset += entry->record_length;
    }

    return true;
}

bool ext2_lookup(
    block_device_t *device,
    const ext2_superblock_t *superblock,
    const ext2_inode_t *directory,
    const char *name,
    uint32_t *inode_number)
{
    if (device == NULL || superblock == NULL || directory == NULL || name == NULL || inode_number == NULL)
        return false;

    if ((directory->mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
        return false;

    uint32_t block_size = 1024u << superblock->log_block_size;

    if (block_size > 4096)
        return false;

    uint8_t block[4096];

    /*
     * For now search only direct blocks
     */
    for (unsigned i = 0; i < 12; i++)
    {
        uint32_t block_number = directory->block[i];

        if (block_number == 0)
            continue;

        if (!ext2_read_block(device, block_size, block_number, block))
            return false;

        size_t offset = 0;

        while (offset < block_size)
        {
            ext2_directory_entry_t *entry = (ext2_directory_entry_t *)(block + offset);

            if (entry->record_length < 8)
                return false;

            if (offset + entry->record_length > block_size)
                return false;

            if (entry->name_length > entry->record_length - 8)
                return false;

            if (entry->inode != 0)
            {
                size_t wanted_length = strlen(name);

                if (wanted_length == entry->name_length && memcmp(entry->name, name, wanted_length) == 0)
                {
                    *inode_number = entry->inode;
                    return true;
                }
            }

            offset += entry->record_length;
        }
    }

    return false;
}

bool ext2_read_file(
    block_device_t *device,
    const ext2_superblock_t *superblock,
    const ext2_inode_t *inode,
    size_t offset,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_read)
{
    if (device == NULL ||
        superblock == NULL ||
        inode == NULL ||
        buffer == NULL ||
        bytes_read == NULL)
    {
        return false;
    }

    ext2_block_cache_t *cache =
        kmalloc(sizeof(ext2_block_cache_t));

    if (cache == NULL)
        return false;

    memset(cache, 0, sizeof(ext2_block_cache_t));

    bool result = ext2_read_file_cached(
        device,
        superblock,
        inode,
        offset,
        buffer,
        buffer_size,
        bytes_read,
        cache);

    kfree(cache);

    return result;
}

static bool ext2_read_file_cached(
    block_device_t *device,
    const ext2_superblock_t *superblock,
    const ext2_inode_t *inode,
    size_t offset,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_read,
    ext2_block_cache_t *cache)
{
    if (device == NULL || superblock == NULL || inode == NULL || buffer == NULL || bytes_read == NULL)
        return false;

    *bytes_read = 0;

    if ((inode->mode & EXT2_S_IFMT) != EXT2_S_IFREG)
        return false;

    uint32_t block_size = 1024u << superblock->log_block_size;

    if (block_size > 4096)
        return false;

    if (offset >= inode->size_low)
        return true;

    size_t available = inode->size_low - offset;

    size_t wanted = buffer_size < available ? buffer_size : available;

    if (wanted > buffer_size)
        wanted = buffer_size;

    uint8_t block[4096];

    size_t total = 0;

    while (total < wanted)
    {
        size_t file_offset = offset + total;
        uint32_t logical_block = (uint32_t)(file_offset / block_size);
        uint32_t offset_in_block = (uint32_t)(file_offset % block_size);

        uint32_t block_number;

        if (!ext2_get_data_block(device, superblock, inode, logical_block, &block_number, cache))
            return false;

        if (block_number == 0)
            break;

        if (!ext2_read_block(device, block_size, block_number, block))
            return false;

        size_t chunk = block_size - offset_in_block;

        if (chunk > wanted - total)
            chunk = wanted - total;

        memcpy((uint8_t *)buffer + total, block + offset_in_block, chunk);

        total += chunk;
    }

    *bytes_read = total;

    return true;
}

static int ext2_vnode_lookup(vnode_t *directory, const char *name, vnode_t **result)
{
    if (directory == NULL || name == NULL || result == NULL)
        return -1;

    ext2_vnode_data_t *dir_data = (ext2_vnode_data_t *)directory->private_data;

    if (dir_data == NULL || dir_data->fs == NULL)
        return -1;

    ext2_fs_t *fs = dir_data->fs;
    uint32_t inode_number;

    if (!ext2_lookup(fs->device, &fs->superblock, &dir_data->inode, name, &inode_number))
        return -1;

    ext2_inode_t inode;

    if (!ext2_read_inode(fs->device, &fs->superblock, inode_number, &inode))
        return -1;

    vnode_t *node = kmalloc(sizeof(vnode_t));

    if (node == NULL)
        return -1;

    ext2_vnode_data_t *data = kmalloc(sizeof(ext2_vnode_data_t));

    if (data == NULL)
    {
        kfree(node);
        return -1;
    }

    data->fs = fs;
    data->inode_number = inode_number;
    data->inode = inode;
    data->block_cache = NULL;

    node->inode = inode_number;
    node->size = inode.size_low;
    node->private_data = data;
    node->ref_count = 1;

    if ((inode.mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
    {
        node->type = VNODE_DIRECTORY;
        node->ops = &ext2_directory_ops;
    }
    else if ((inode.mode & EXT2_S_IFMT) == EXT2_S_IFREG)
    {
        node->type = VNODE_REGULAR;
        node->ops = &ext2_file_ops;
    }
    else
    {
        kfree(data);
        kfree(node);
        return -1;
    }

    *result = node;

    return 0;
}

static int ext2_vnode_read(vnode_t *node, size_t offset, void *buffer, size_t size, size_t *bytes_read)
{
    if (node == NULL || buffer == NULL || bytes_read == NULL)
        return -1;

    ext2_vnode_data_t *data = (ext2_vnode_data_t *)node->private_data;

    if (data == NULL || data->fs == NULL)
        return -1;

    ext2_fs_t *fs = data->fs;

    if (data->block_cache == NULL)
    {
        data->block_cache = kmalloc(sizeof(ext2_block_cache_t));

        if (data->block_cache == NULL)
            return -1;

        memset(data->block_cache, 0, sizeof(ext2_block_cache_t));
    }

    if (!ext2_read_file_cached(fs->device, &fs->superblock, &data->inode, offset, buffer, size, bytes_read, data->block_cache))
        return -1;

    return 0;
}

bool ext2_mount(block_device_t *device, ext2_fs_t *fs)
{
    if (device == NULL || fs == NULL)
        return false;

    if (!ext2_read_superblock(device, &fs->superblock))
        return false;

    fs->device = device;

    ext2_inode_t root_inode;

    if (!ext2_read_inode(device, &fs->superblock, EXT2_ROOT_INODE, &root_inode))
        return false;

    if ((root_inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
        return false;

    ext2_vnode_data_t *root_data = kmalloc(sizeof(ext2_vnode_data_t));

    if (root_data == NULL)
        return false;

    root_data->fs = fs;
    root_data->inode_number = EXT2_ROOT_INODE;
    root_data->inode = root_inode;
    root_data->block_cache = NULL;

    fs->root_vnode.type = VNODE_DIRECTORY;
    fs->root_vnode.inode = EXT2_ROOT_INODE;
    fs->root_vnode.size = root_inode.size_low;
    fs->root_vnode.private_data = root_data;
    fs->root_vnode.ops = &ext2_directory_ops;
    fs->root_vnode.ref_count = 0;

    return true;
}

static bool ext2_get_data_block(
    block_device_t *device,
    const ext2_superblock_t *superblock,
    const ext2_inode_t *inode,
    uint32_t logical_block,
    uint32_t *physical_block,
    ext2_block_cache_t *cache)
{
    if (device == NULL ||
        superblock == NULL ||
        inode == NULL ||
        physical_block == NULL ||
        cache == NULL)
    {
        return false;
    }

    /*
     * Direct blocks: logical blocks 0..11.
     */
    if (logical_block < 12)
    {
        *physical_block = inode->block[logical_block];
        return true;
    }

    uint32_t block_size =
        1024u << superblock->log_block_size;

    if (block_size > 4096)
        return false;

    uint32_t entries_per_block =
        block_size / sizeof(uint32_t);

    if (entries_per_block == 0)
        return false;

    uint32_t index = logical_block - 12;

    /*
     * Single indirect.
     */
    if (index < entries_per_block)
    {
        uint32_t indirect_block =
            inode->block[12];

        if (indirect_block == 0)
        {
            *physical_block = 0;
            return true;
        }

        if (!cache->single_valid ||
            cache->single_block_number != indirect_block)
        {
            if (!ext2_read_block(
                    device,
                    block_size,
                    indirect_block,
                    cache->single_entries))
            {
                return false;
            }

            cache->single_block_number =
                indirect_block;

            cache->single_valid = true;
        }

        *physical_block =
            cache->single_entries[index];

        return true;
    }

    index -= entries_per_block;

    /*
     * Double indirect.
     */
    uint64_t double_capacity =
        (uint64_t)entries_per_block *
        (uint64_t)entries_per_block;

    if ((uint64_t)index < double_capacity)
    {
        uint32_t root_block =
            inode->block[13];

        if (root_block == 0)
        {
            *physical_block = 0;
            return true;
        }

        uint32_t first_level_index =
            index / entries_per_block;

        uint32_t second_level_index =
            index % entries_per_block;

        /*
         * Cache the double-indirect root block.
         */
        if (!cache->double_root_valid ||
            cache->double_root_block_number != root_block)
        {
            if (!ext2_read_block(
                    device,
                    block_size,
                    root_block,
                    cache->double_root_entries))
            {
                return false;
            }

            cache->double_root_block_number =
                root_block;

            cache->double_root_valid = true;

            /*
             * The cached leaf belongs to the previous
             * root contents, so invalidate it.
             */
            cache->double_leaf_valid = false;
        }

        uint32_t leaf_block =
            cache->double_root_entries[first_level_index];

        if (leaf_block == 0)
        {
            *physical_block = 0;
            return true;
        }

        /*
         * Cache the currently-used second-level block.
         */
        if (!cache->double_leaf_valid ||
            cache->double_leaf_block_number != leaf_block)
        {
            if (!ext2_read_block(
                    device,
                    block_size,
                    leaf_block,
                    cache->double_leaf_entries))
            {
                return false;
            }

            cache->double_leaf_block_number =
                leaf_block;

            cache->double_leaf_valid = true;
        }

        *physical_block =
            cache->double_leaf_entries[second_level_index];

        return true;
    }

    /*
     * Triple indirect is not supported yet.
     */
    return false;
}
