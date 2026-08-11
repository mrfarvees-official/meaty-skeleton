#ifndef KERNEL_ATA_H
#define KERNEL_ATA_H

#include <stdbool.h>
#include <kernel/block_device.h>

bool ata_initialize(void);
block_device_t* ata_primary_master(void);

#endif