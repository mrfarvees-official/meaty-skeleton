#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <kernel/partition.h>
#include <kernel/block_device.h>

static int partition_read(block_device_t *device, uint64_t lba, uint32_t count, void *buffer)
{
    if (device == NULL || buffer == NULL || count == 0) return -1;

    partition_device_t *partition = (partition_device_t*)device->private_data;

    if (partition == NULL || partition->parent == NULL) return -1;

    if (partition->parent->read == NULL) return -1;

    if (lba >= partition->sector_count) return -1;

    if ((uint64_t)count > partition->sector_count - lba) return -1;

    return block_read(partition->parent, partition->start_lba + lba, count, buffer);
}

static int partition_write(block_device_t *device, uint64_t lba, uint32_t count, const void *buffer)
{
    if (device == NULL || buffer == NULL || count == 0) return -1;

    partition_device_t *partition = (partition_device_t*)device->private_data;

    if (partition == NULL || partition->parent == NULL) return -1;

    if (partition->parent->write == NULL) return -1;

    if (lba >= partition->sector_count) return -1;

    if ((uint64_t)count > partition->sector_count - lba) return -1;

    return block_write(partition->parent, partition->start_lba + lba, count, buffer);
}

bool partition_from_mbr(
    block_device_t *disk,
    unsigned index,
    partition_device_t *partition)
{
    if (disk == NULL ||
        partition == NULL ||
        index >= MBR_PARTITION_COUNT)
    {
        return false;
    }

    if (disk->sector_size != 512)
        return false;

    uint8_t sector[512];

    if (block_read(
            disk,
            0,
            1,
            sector) != 0)
    {
        printf("MBR: failed to read sector 0\n");
        return false;
    }

    printf(
    "MBR: signature = %02x %02x\n",
    (unsigned)sector[510],
    (unsigned)sector[511]);

    /*
     * Standard MBR signature.
     */
    if (sector[510] != 0x55 ||
        sector[511] != 0xAA)
    {
        printf("MBR: invalid signature\n");
        return false;
    }

    const mbr_partition_entry_t *entries =
        (const mbr_partition_entry_t *)&sector[446];

    const mbr_partition_entry_t *entry =
        &entries[index];

    /*
     * Empty partition entry.
     */
    if (entry->type == 0 ||
        entry->sector_count == 0)
    {
        return false;
    }

    /*
     * Partition must begin inside the parent disk.
     */
    if ((uint64_t)entry->start_lba >=
        disk->sector_count)
    {
        return false;
    }

    /*
     * Partition must completely fit inside the parent disk.
     *
     * Written this way instead of:
     *
     * start + count > disk_size
     *
     * to avoid integer overflow.
     */
    if ((uint64_t)entry->sector_count >
        disk->sector_count -
        (uint64_t)entry->start_lba)
    {
        return false;
    }

    partition->parent =
        disk;

    partition->start_lba =
        entry->start_lba;

    partition->sector_count =
        entry->sector_count;

    partition->block.name =
        "partition";

    partition->block.sector_size =
        disk->sector_size;

    partition->block.sector_count =
        entry->sector_count;

    partition->block.read =
        partition_read;

    partition->block.write =
        partition_write;

    partition->block.private_data =
        partition;

    return true;
}