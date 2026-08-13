#ifndef KERNEL_AHCI_H
#define KERNEL_AHCI_H

#include <stdbool.h>

#include <kernel/block_device.h>
#include <kernel/pci.h>

bool ahci_probe(const pci_device_t *device);
block_device_t *ahci_primary_disk(void);

#endif