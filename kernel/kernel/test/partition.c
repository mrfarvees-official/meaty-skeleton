#include <kernel/test.h>
#include <kernel/ata.h>
#include <kernel/partition.h>
#include <stdio.h>

static partition_device_t root_partition;

void partition_test(void)
{
    block_device_t *disk =
        ata_primary_master();

    if (disk == NULL)
    {
        printf("Partition: no ATA disk\n");
        return;
    }

    if (!partition_from_mbr(
            disk,
            0,
            &root_partition))
    {
        printf("Partition: MBR partition 0 not found\n");
        return;
    }

    printf(
        "Partition 0: start=%u sectors=%u\n",
        (unsigned)root_partition.start_lba,
        (unsigned)root_partition.sector_count);
}

void ext2_magic_test(void)
{
    block_device_t *partition =
        &root_partition.block;

    uint8_t sector[512];

    if (block_read(
            partition,
            2,
            1,
            sector) != 0)
    {
        printf("EXT2: superblock read failed\n");
        return;
    }

    printf(
        "EXT2: magic bytes = %02x %02x\n",
        (unsigned)sector[56],
        (unsigned)sector[57]);

    uint16_t magic =
        (uint16_t)sector[56] |
        ((uint16_t)sector[57] << 8);

    printf(
        "EXT2: magic = %04x\n",
        (unsigned)magic);
}