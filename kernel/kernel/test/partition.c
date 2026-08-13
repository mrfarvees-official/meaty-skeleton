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

    ext2_block_group_descriptor_t bgd;
    if (!ext2_read_group_descriptor(&partitions[0].block, &sb, 0, &bgd))
    {
        printf("EXT2: failed reading group descriptor\n");
        return;
    }
    printf("EXT2: group 0\n");
    printf("EXT2: block bitmap=%u\n", (unsigned)bgd.block_bitmap);
    printf("EXT2: inode bitmap=%u\n", (unsigned)bgd.inode_bitmap);
    printf("EXT2: inode table=%u\n", (unsigned)bgd.inode_table);

    ext2_inode_t root_inode;
    if (!ext2_read_inode(&partitions[0].block, &sb, EXT2_ROOT_INODE, &root_inode))
    {
        printf("EXT2: failed reading root inode\n");
        return;
    }
    printf("EXT2: root inode\n");
    printf("EXT2: root mode=%04x\n", (unsigned)root_inode.mode);
    printf("EXT2: root size=%u\n", (unsigned)root_inode.size_low);
    printf("EXT2: root block0=%u\n", (unsigned)root_inode.block[0]);
    printf("EXT2: root links=%u\n", (unsigned)root_inode.link_count);
    if ((root_inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
    {
        printf("EXT2: root inode is NOT directory\n");
        return;
    }
    printf("EXT2: root inode is directory\n");

    printf("Partition scan: found %u partitions\n", (unsigned)count);

    if (!ext2_list_directory(&partitions[0].block, &sb, &root_inode))
    {
        printf("EXT2: failed to listing root directory\n");
        return;
    }
    printf("EXT2: root directory listed\n");

    uint32_t inode_number;
    if (!ext2_lookup(&partitions[0].block, &sb, &root_inode, "lost+found", &inode_number))
    {
        printf("EXT2: lost+found not found\n");
        return;
    }
    printf("EXT2: lost+found inode=%u\n", (unsigned)inode_number);
    ext2_inode_t found_inode;
    if (!ext2_read_inode(&partitions[0].block, &sb, inode_number, &found_inode))
    {
        printf("EXT2: failed reading found inode\n");
        return;
    }
    printf("EXT2: found mode=%04x size=%u\n", (unsigned)found_inode.mode, (unsigned)found_inode.size_low);

    uint32_t hello_inode_number;
    if (!ext2_lookup(
            &partitions[0].block,
            &sb,
            &root_inode,
            "hello.txt",
            &hello_inode_number))
    {
        printf("EXT2: hello.txt not found\n");
        return;
    }
    printf(
        "EXT2: hello.txt inode=%u\n",
        (unsigned)hello_inode_number);
    ext2_inode_t hello_inode;
    if (!ext2_read_inode(
            &partitions[0].block,
            &sb,
            hello_inode_number,
            &hello_inode))
    {
        printf("EXT2: failed reading hello.txt inode\n");
        return;
    }

    char buffer[128];
    size_t bytes_read;
    if (!ext2_read_file(
            &partitions[0].block,
            &sb,
            &hello_inode,
            0,
            buffer,
            sizeof(buffer) - 1,
            &bytes_read))
    {
        printf("EXT2: failed reading hello.txt\n");
        return;
    }
    buffer[bytes_read] = '\0';
    printf(
        "EXT2: hello.txt: %s\n",
        buffer);

    for (size_t i = 0; i < count; i++)
    {
        printf("partition %u: start=%u sectors=%u\n", (unsigned)i, (unsigned)partitions[i].start_lba, (unsigned)partitions[i].sector_count);
    }
}