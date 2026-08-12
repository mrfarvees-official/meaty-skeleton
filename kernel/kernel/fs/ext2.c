#include <string.h>

#include <kernel/ext2.h>
#include <kernel/block_device.h>

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