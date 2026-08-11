#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <kernel/ata.h>

#include "../arch/i386/io.h"

/*
 * Primary IDE channel.
 */
#define ATA_PRIMARY_IO          0x1F0
#define ATA_PRIMARY_CONTROL     0x3F6

/*
 * Registers relative to ATA_PRIMARY_IO.
 */
#define ATA_REG_DATA            0x00
#define ATA_REG_ERROR           0x01
#define ATA_REG_FEATURES        0x01
#define ATA_REG_SECTOR_COUNT    0x02
#define ATA_REG_LBA_LOW         0x03
#define ATA_REG_LBA_MID         0x04
#define ATA_REG_LBA_HIGH        0x05
#define ATA_REG_DRIVE           0x06
#define ATA_REG_STATUS          0x07
#define ATA_REG_COMMAND         0x07

/*
 * ATA commands.
 */
#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_IDENTIFY        0xEC

/*
 * Status register bits.
 */
#define ATA_STATUS_ERR          0x01
#define ATA_STATUS_DRQ          0x08
#define ATA_STATUS_DF           0x20
#define ATA_STATUS_DRDY         0x40
#define ATA_STATUS_BSY          0x80

/*
 * We only support 28-bit LBA for now.
 */
#define ATA_LBA28_MAX           0x0FFFFFFFu
#define ATA_SECTOR_SIZE         512u

/*
 * Arbitary polling timeout.
 * 
 * This prevents a broken/nonexistent controller from hanging
 * the kernel forever.
 */
#define ATA_TIMEOUT             1000000u

static block_device_t primary_master;
static bool primary_master_present;

/*
 * ATA requires a small dely after selecting a drive or 
 * issuing certain commands.
 * 
 * Reading the alternate status registers four times gives
 * enough delay for class ATA PIO.
 */
static void ata_delay_400ns(void)
{
    ata_inb(ATA_PRIMARY_CONTROL);
    ata_inb(ATA_PRIMARY_CONTROL);
    ata_inb(ATA_PRIMARY_CONTROL);
    ata_inb(ATA_PRIMARY_CONTROL);
}

/*
 * Wait until BSY clears.
 */
static int ata_wait_not_busy(void)
{
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++)
    {
        uint8_t status = ata_inb(ATA_PRIMARY_IO + ATA_REG_STATUS);

        if ((status & ATA_STATUS_BSY) == 0) return 0;
    }

    return -1;
}

/*
 * Wait until the drive has data ready.
 *
 * Returns:
 *  
 *  0 success
 * -1 error
 * -2 timeout
 */
static int ata_wait_drq(void)
{
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++)
    {
        uint8_t status = ata_inb(ATA_PRIMARY_IO + ATA_REG_STATUS);

        if (status & ATA_STATUS_ERR) return -1;

        if (status & ATA_STATUS_DF) return -1;

        if ((status & ATA_STATUS_BSY) == 0 && (status & ATA_STATUS_DRQ)) return 0;
    }

    return -2;
}

static bool ata_identify_primary_master(uint16_t identify[256])
{
    /*
     * Select primary master.
     *
     * 0xA0
     * bit 7 = 1
     * bit 5 = 1
     * bit 4 = 0 -> master
     */
    ata_outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xA0);
    ata_delay_400ns();

    /*
     * IDENTIFY requires these registers to be zero
     */
    ata_outb(ATA_PRIMARY_IO + ATA_REG_SECTOR_COUNT, 0);
    ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_LOW, 0);
    ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, 0);
    ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH, 0);

    /*
     * Send IDENTIFY DEVICE
     */
    ata_outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    /*
     * A status value of zero normally means no device exists
     * on this bus position
     */
    uint8_t status = ata_inb(ATA_PRIMARY_IO + ATA_REG_STATUS);

    if (status == 0) return false;

    /*
     * Wait for BSY clear.
     */
    if (ata_wait_not_busy() != 0) return false;

    /*
     * For ATA devices these should remain zero after IDENTIFY.
     *
     * Non-zero values commonly indicate an ATAPI device such
     * as a CD-ROM
     */
    uint8_t lba_mid = ata_inb(ATA_PRIMARY_IO + ATA_REG_LBA_MID);
    uint8_t lba_high = ata_inb(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH);

    if (lba_mid != 0 || lba_high != 0) return 0;

    if (ata_wait_drq() != 0) return false;

    /*
     * IDENTIFY returns exactly 256 words = 512 bytes.
     */
    for (size_t i = 0; i < 256; i++)
        identify[i] = ata_inw(ATA_PRIMARY_IO + ATA_REG_DATA);

    return true;
}

static uint32_t ata_identify_sector_count(const uint16_t identify[256])
{
    return ((uint32_t)identify[61] << 16) | (uint32_t)identify[60];
}

static void ata_identify_model(const uint16_t indentify[256], char model[41])
{
    for (size_t i = 0; i < 20; i++)
    {
        uint16_t word = indentify[27 + i];
        model[i * 2] = (char)(word >> 8);
        model[i * 2 + 1] = (char)(word & 0xFF);
    }

    model[40] = '\0';

    /*
     * Trim trailing spaces.
     */
    for (int i = 39; i >= 0; i--)
    {
        if (model[i] == ' ') model[i] = '\0';
        else break;
    }
}

static int ata_read_sector28(uint32_t lba, void *buffer)
{
    if (buffer == NULL) return -1;

    if (lba  > ATA_LBA28_MAX) return -1;

    /*
     * Wait for previous command to finish.
     */
    if (ata_wait_not_busy() != 0) return -1;

    /*
     * Select primary master and place bits 24...27 of 
     * the LBA into the drive/head register.
     * 
     * 0xE0
     * 
     * bit 7 = 1
     * bit 6 = 1 -> LBA mode
     * bit 5 = 1
     * bit 4 = 0 -> master
     */
    ata_outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay_400ns();

    ata_outb(ATA_PRIMARY_IO + ATA_REG_SECTOR_COUNT, 1);

    ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_LOW, (uint8_t)(lba & 0xFF));
    ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));

    /*
     * Start the transfer.
     */
    ata_outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    if (ata_wait_drq() != 0) return -1;

    /*
     * ATA tranfers 256 16-bit words for a 512-byte sector.
     */
    uint16_t *words = (uint16_t*)buffer;

    for (size_t i = 0; i < 256; i++)
        words[i] = ata_inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    
    ata_delay_400ns();
    
    return 0;
}

static int ata_block_read(block_device_t *device, uint64_t lba, uint32_t count, void *buffer)
{
    if (device == NULL || buffer == NULL || count == 0) return -1;

    /*
     * This first driver only support 28-bit LBA.
     */
    if (lba > ATA_LBA28_MAX) return -1;

    if ((uint64_t)count > ((uint64_t)ATA_LBA28_MAX + 1u) - lba) return -1;

    uint8_t *destination = (uint8_t*)buffer;

    for (uint32_t i = 0; i < count; i++)
    {
        if (ata_read_sector28((uint32_t)(lba + i), destination) != 0) return -1;
    
        destination += ATA_SECTOR_SIZE;
    }

    return 0;
}

bool ata_initialize(void)
{
    uint16_t identify[256];

    primary_master_present = false;

    if (!ata_identify_primary_master(identify))
    {
        printf("ATA: primary master not detected\n");
        return false;
    }

    uint32_t sector_count = ata_identify_sector_count(identify);

    if (sector_count == 0)
    {
        printf("ATA: invalid sector count\n");
        return false;
    }

    char model[41];

    ata_identify_model(identify, model);
    
    primary_master.name = "ata0";
    primary_master.sector_size = ATA_SECTOR_SIZE;
    primary_master.sector_count = sector_count;
    primary_master.read = ata_block_read;

    /*
     * Read -only for now
     */
    primary_master.write = NULL;
    primary_master.private_data = NULL;

    primary_master_present = true;

    printf("ATA: primary master detected\n");
    printf("ATA: model: %s\n", model);
    printf("ATA: sectors: %u\n", (unsigned)sector_count);
    printf("ATA: capacity: %u\n", (unsigned)((uint64_t)sector_count * ATA_SECTOR_SIZE / (1024u * 1024u)));

    return true;
}

block_device_t* ata_primary_master(void)
{
    if (!primary_master_present) return NULL;

    return &primary_master;
}