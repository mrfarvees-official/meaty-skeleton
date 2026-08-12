#include <kernel/test.h>
#include <kernel/ata.h>
#include <kernel/partition.h>
#include <kernel/block_device.h>
#include <kernel/ext2.h>
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

void partition_scan_test(void)
{
    block_device_t *disk =
        ata_primary_master();

    if (disk == NULL)
    {
        printf("Partition scan: no disk\n");
        return;
    }

    partition_device_t partitions[8];

    size_t count =
        partition_scan(
            disk,
            partitions,
            8);

    if (count == 0)
    {
        printf("EXT2 test: no partition\n");
        return;
    }

    ext2_superblock_t sb;

    if (!ext2_read_superblock(&partitions[0].block, &sb))
    {
        printf("EXT2: superblock invalid\n");
        return;
    }

    uint32_t block_size = 1024u << sb.log_block_size;

    printf("EXT2: detected\n");
    printf("EXT2: magic=%04x\n", (unsigned)sb.magic);
    printf("EXT2: blocks=%u\n", (unsigned)sb.block_count);
    printf("EXT2: inodes=%u\n", (unsigned)sb.inode_count);
    printf("EXT2: block size=%u\n", (unsigned)block_size);
    printf("EXT2: blocks/group=%u\n", (unsigned)sb.blocks_per_group);
    printf("EXT2: inodes/group=%u\n", (unsigned)sb.inodes_per_group);
    printf("EXT2: inode size=%u\n", (unsigned)sb.inode_size);

    printf("Partition scan: found %u partitions\n", (unsigned)count);

    for (size_t i = 0; i < count; i++)
    {
        printf("partition %u: start=%u sectors=%u\n", (unsigned)i, (unsigned)partitions[i].start_lba, (unsigned)partitions[i].sector_count);
    }
}