#include <kernel/test.h>
#include <kernel/ata.h>
#include <kernel/block_device.h>
#include <stdint.h>
#include <stdio.h>

void ata_test(void)
{
    block_device_t *disk =
        ata_primary_master();

    if (disk == NULL)
    {
        printf(
            "ATA test: no primary master\n");

        return;
    }


    uint8_t sector[512];


    if (block_read(
            disk,
            0,
            1,
            sector) != 0)
    {
        printf(
            "ATA test: sector 0 read failed\n");

        return;
    }


    printf(
        "ATA: sector 0 read OK\n");

    printf(
        "ATA: sector 0 signature: %02x %02x\n",
        (unsigned)sector[510],
        (unsigned)sector[511]);
}