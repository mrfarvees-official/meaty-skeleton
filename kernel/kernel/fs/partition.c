#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <kernel/ata.h>
#include <kernel/partition.h>
#include <kernel/block_device.h>

static void partition_initialize(partition_device_t *partition, block_device_t *parent, uint64_t start_lba, uint64_t sector_count, const char *name);
static uint32_t crc32_update(uint32_t crc, const void *data, size_t length);
static uint32_t crc32_finish(uint32_t crc);

static int partition_read(block_device_t *device, uint64_t lba, uint32_t count, void *buffer)
{
    if (device == NULL || buffer == NULL || count == 0)
        return -1;

    partition_device_t *partition = (partition_device_t *)device->private_data;

    if (partition == NULL || partition->parent == NULL)
        return -1;

    if (partition->parent->read == NULL)
        return -1;

    if (lba >= partition->sector_count)
        return -1;

    if ((uint64_t)count > partition->sector_count - lba)
        return -1;

    return block_read(partition->parent, partition->start_lba + lba, count, buffer);
}

static int partition_write(block_device_t *device, uint64_t lba, uint32_t count, const void *buffer)
{
    if (device == NULL || buffer == NULL || count == 0)
        return -1;

    partition_device_t *partition = (partition_device_t *)device->private_data;

    if (partition == NULL || partition->parent == NULL)
        return -1;

    if (partition->parent->write == NULL)
        return -1;

    if (lba >= partition->sector_count)
        return -1;

    if ((uint64_t)count > partition->sector_count - lba)
        return -1;

    return block_write(partition->parent, partition->start_lba + lba, count, buffer);
}

bool partition_from_mbr(block_device_t *disk, unsigned index, partition_device_t *partition)
{
    if (disk == NULL || partition == NULL || index >= MBR_PARTITION_COUNT)
        return false;

    if (disk->sector_size != 512)
        return false;

    uint8_t sector[512];

    if (block_read(disk, 0, 1, sector) != 0)
        return false;

    /*
     * Standard MBR signature.
     */
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return false;

    const mbr_partition_entry_t *entries = (const mbr_partition_entry_t *)&sector[446];

    const mbr_partition_entry_t *entry = &entries[index];

    /*
     * Empty partition entry.
     */
    if (entry->type == 0 || entry->sector_count == 0)
        return false;

    /*
     * Partition must begin inside the parent disk.
     */
    if ((uint64_t)entry->start_lba >= disk->sector_count)
        return false;

    /*
     * Partition must completely fit inside the parent disk.
     *
     * Written this way instead of:
     *
     * start + count > disk_size
     *
     * to avoid integer overflow.
     */
    if ((uint64_t)entry->sector_count > disk->sector_count - (uint64_t)entry->start_lba)
        return false;

    partition_initialize(
        partition,
        disk,
        entry->start_lba,
        entry->sector_count,
        "mbr-partition");

    return true;
}

bool partition_from_gpt(block_device_t *disk, unsigned index, partition_device_t *partition)
{
    if (disk == NULL || partition == NULL)
        return false;

    if (disk->sector_size != 512)
        return false;

    uint8_t header_sector[512];
    uint8_t entry_sector[512];

    if (block_read(disk, 1, 1, header_sector) != 0)
    {
        printf("GPT: failed to read header\n");
        return false;
    }

    const gpt_header_t *header = (const gpt_header_t *)header_sector;

    if (header->signature != GPT_HEADER_SIGNATURE)
    {
        printf("GPT: invalid signature\n");
        return false;
    }

    /*
     * GPT v1 header is at least 92 bytes.
     */
    if (header->header_size < 92 || header->header_size > disk->sector_size)
    {
        printf("GPT: invalid header size\n");
        return false;
    }

    /*
     * Validate GPT header CRC32.
     *
     * The header_crc32 field must be treated as zero
     * during CRC calculation.
     */
    uint8_t header_copy[512];

    memcpy(
        header_copy,
        header_sector,
        header->header_size);

    gpt_header_t *header_for_crc =
        (gpt_header_t *)header_copy;

    uint32_t expected_header_crc =
        header_for_crc->header_crc32;

    header_for_crc->header_crc32 = 0;

    uint32_t actual_header_crc =
        crc32_compute(
            header_copy,
            header->header_size);

    if (actual_header_crc != expected_header_crc)
    {
        printf("GPT: header CRC mismatch\n");
        return false;
    }

    if (header->partition_entry_size < sizeof(gpt_partition_entry_t))
    {
        printf("GPT: partition entry too small\n");
        return false;
    }

    if (index >= header->partition_entry_count)
        return false;

    if (header->partition_entries_lba >= disk->sector_count)
    {
        printf("GPT: invalid entry table LBA\n");
        return false;
    }

    uint64_t table_bytes = (uint64_t)header->partition_entry_count * (uint64_t)header->partition_entry_size;

    uint64_t table_sectors = (table_bytes + disk->sector_size - 1) / disk->sector_size;

    if (table_sectors > disk->sector_count - header->partition_entries_lba)
    {
        printf("GPT: partition table outside disk\n");
        return false;
    }

    uint32_t crc = 0xFFFFFFFFu;

    uint64_t remaining = table_bytes;
    uint64_t table_lba = header->partition_entries_lba;

    uint8_t table_sector[512];

    while (remaining > 0)
    {
        if (block_read(disk, table_lba, 1, table_sector) != 0)
        {
            printf("GPT: failed to read partition table\n");
            return false;
        }

        size_t chunk = remaining < disk->sector_size ? (size_t)remaining : (size_t)disk->sector_size;

        crc = crc32_update(crc, table_sector, chunk);

        remaining -= chunk;
        table_lba++;
    }

    crc = crc32_finish(crc);

    if (crc != header->partition_entries_crc32)
    {
        printf("GPT: partition table CRC mismatch\n");
        return false;
    }

    /*
     * Convert entry index into byte offset in
     * the partition entry array
     */
    uint64_t entry_offset = (uint64_t)index * (uint64_t)header->partition_entry_size;
    uint64_t entry_lba = header->partition_entries_lba + entry_offset / disk->sector_size;
    uint32_t offset_in_sector = (uint32_t)(entry_offset % disk->sector_size);

    if (entry_lba >= disk->sector_count)
        return false;

    /*
     * Temporary limitation:
     * require the whole GPT entry to fit
     * inside one disk sector
     */
    if ((uint64_t)offset_in_sector + header->partition_entry_size > disk->sector_size)
    {
        printf("GPT: partition entry crosses sector\n");
        return false;
    }

    if (block_read(disk, entry_lba, 1, entry_sector) != 0)
    {
        printf("GPT: failed to read partition entry\n");
        return false;
    }

    const gpt_partition_entry_t *entry = (const gpt_partition_entry_t *)(entry_sector + offset_in_sector);

    /*
     * Zero type GUID means unused GPT entry.
     */
    bool empty = true;

    for (size_t i = 0; i < 16; i++)
    {
        if (entry->type_guid[i] != 0)
        {
            empty = false;
            break;
        }
    }

    if (empty)
        return false;

    if (entry->first_lba > entry->last_lba)
        return false;

    if (entry->first_lba >= disk->sector_count)
        return false;

    if (entry->last_lba >= disk->sector_count)
        return false;

    /*
     * Also enforce GPT usable-region limits.
     */
    if (entry->first_lba < header->first_usable_lba || entry->last_lba > header->last_usable_lba)
    {
        printf("GPT: partition outside usable range\n");
        return false;
    }

    uint64_t sector_count = entry->last_lba - entry->first_lba + 1;

    partition_initialize(
        partition,
        disk,
        entry->first_lba,
        sector_count,
        "gpt-partition");

    return true;
}

static void partition_initialize(partition_device_t *partition, block_device_t *parent, uint64_t start_lba, uint64_t sector_count, const char *name)
{
    partition->parent = parent;
    partition->start_lba = start_lba;
    partition->sector_count = sector_count;
    partition->block.name = name;
    partition->block.sector_size = parent->sector_size;
    partition->block.sector_count = sector_count;
    partition->block.read = partition_read;
    partition->block.write = partition_write;
    partition->block.private_data = partition;
}

static bool mbr_is_protective_gpt(block_device_t *disk)
{
    uint8_t sector[512];

    if (block_read(disk, 0, 1, sector) != 0)
        return false;

    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return false;

    const mbr_partition_entry_t *entries = (const mbr_partition_entry_t *)&sector[446];

    for (unsigned i = 0; i < MBR_PARTITION_COUNT; i++)
    {
        if (entries[i].type == 0xEE)
            return true;
    }

    return false;
}

uint32_t crc32_compute(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= bytes[i];

        for (unsigned bit = 0; bit < 8; bit++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
    }

    return ~crc;
}

static uint32_t crc32_update(uint32_t crc, const void *data, size_t length)
{
    const uint8_t *bytes =
        (const uint8_t *)data;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= bytes[i];

        for (unsigned bit = 0; bit < 8; bit++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
    }

    return crc;
}

static uint32_t crc32_finish(uint32_t crc)
{
    return ~crc;
}

static size_t partition_scan_mbr(block_device_t *disk, partition_device_t *partitions, size_t max_partitions)
{
    if (disk == NULL || partitions == NULL || max_partitions == 0)
        return 0;

    size_t found = 0;

    for (unsigned i = 0; i < MBR_PARTITION_COUNT && found < max_partitions; i++)
    {
        if (partition_from_mbr(disk, i, &partitions[found]))
            found++;
    }

    return found;
}

static size_t partition_scan_gpt(block_device_t *disk, partition_device_t *partitions, size_t max_partitions)
{
    if (disk == NULL || partitions == NULL || max_partitions == 0)
    {
        return 0;
    }

    if (disk->sector_size != 512) 
        return 0;

    uint8_t header_sector[512];

    if (block_read(disk, 1, 1, header_sector) != 0) 
        return 0;

    const gpt_header_t *header = (const gpt_header_t *)header_sector;

    if (header->signature != GPT_HEADER_SIGNATURE)
        return 0;

    if (header->header_size < 92 || header->header_size > disk->sector_size)
    {
        return 0;
    }

    if (header->partition_entry_size < sizeof(gpt_partition_entry_t))
    {
        return 0;
    }

    /*
     * Validate header CRC once here.
     */
    uint8_t header_copy[512];

    memcpy(header_copy, header_sector, header->header_size);

    gpt_header_t *crc_header = (gpt_header_t *)header_copy;

    uint32_t expected_crc = crc_header->header_crc32;

    crc_header->header_crc32 = 0;

    if (crc32_compute(header_copy, header->header_size) != expected_crc)
    {
        printf("GPT: header CRC mismatch\n");
        return 0;
    }

    /*
     * Validate partition table CRC once.
     */
    uint64_t table_bytes = (uint64_t)header->partition_entry_count * header->partition_entry_size;

    uint64_t remaining = table_bytes;
    uint64_t table_lba = header->partition_entries_lba;

    uint32_t crc = 0xFFFFFFFFu;

    uint8_t sector[512];

    while (remaining > 0)
    {
        if (block_read(disk, table_lba, 1, sector) != 0)
        {
            return 0;
        }

        size_t chunk = remaining < 512 ? (size_t)remaining : 512;

        crc = crc32_update(crc, sector, chunk);

        remaining -= chunk;
        table_lba++;
    }

    crc = crc32_finish(crc);

    if (crc != header->partition_entries_crc32)
    {
        printf("GPT: partition table CRC mismatch\n");
        return 0;
    }

    /*
     * Now enumerate entries WITHOUT calling
     * partition_from_gpt().
     */
    size_t found = 0;

    for (uint32_t index = 0; index < header->partition_entry_count && found < max_partitions; index++)
    {
        uint64_t byte_offset = (uint64_t)index * header->partition_entry_size;

        uint64_t entry_lba = header->partition_entries_lba + byte_offset / disk->sector_size;

        uint32_t offset = (uint32_t)(byte_offset % disk->sector_size);

        /*
         * Still using your temporary restriction.
         */
        if ((uint64_t)offset + header->partition_entry_size > disk->sector_size)
        {
            continue;
        }

        uint8_t entry_sector[512];

        if (block_read(disk, entry_lba, 1, entry_sector) != 0)
        {
            continue;
        }

        const gpt_partition_entry_t *entry = (const gpt_partition_entry_t *)(entry_sector + offset);

        /*
         * Zero type GUID = unused entry.
         */
        bool empty = true;

        for (size_t j = 0; j < 16; j++)
        {
            if (entry->type_guid[j] != 0)
            {
                empty = false;
                break;
            }
        }

        if (empty) continue;

        /*
         * Validate partition range.
         */
        if (entry->first_lba > entry->last_lba)
        {
            continue;
        }

        if (entry->first_lba < header->first_usable_lba)
        {
            continue;
        }

        if (entry->last_lba > header->last_usable_lba)
        {
            continue;
        }

        if (entry->last_lba >= disk->sector_count)
        {
            continue;
        }

        uint64_t sector_count = entry->last_lba - entry->first_lba + 1;

        partition_initialize(&partitions[found], disk, entry->first_lba, sector_count, "gpt-partition");

        found++;
    }

    return found;
}

size_t partition_scan(block_device_t *disk, partition_device_t *partitions, size_t max_partitions)
{
    if (disk == NULL || partitions == NULL || max_partitions == 0)
        return 0;

    if (mbr_is_protective_gpt(disk))
    {
        printf("Partition: GPT detected\n");
        return partition_scan_gpt(disk, partitions, max_partitions);
    }

    printf("Partition: MBR detected\n");
    return partition_scan_mbr(disk, partitions, max_partitions);
}