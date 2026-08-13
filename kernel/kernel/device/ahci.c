#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

#include <kernel/ahci.h>
#include <kernel/paging.h>
#include <kernel/pci.h>

#define PCI_COMMAND_REGISTER 0x04u
#define PCI_BAR5             0x24u

#define PCI_COMMAND_MEMORY_SPACE 0x0002u
#define PCI_COMMAND_BUS_MASTER   0x0004u

#define AHCI_MMIO_SIZE 0x1100u

typedef volatile struct
{
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
} ahci_hba_header_t;

bool ahci_probe(const pci_device_t *device)
{
    if (device == NULL)
        return false;

    /*
     * BAR5 is the AHCI ABAR.
     */
    uint32_t bar5 =
        pci_config_read32(
            device->bus,
            device->device,
            device->function,
            PCI_BAR5);

    /*
     * AHCI ABAR must be a memory BAR.
     */
    if ((bar5 & 0x01u) != 0)
    {
        printf("AHCI: BAR5 is not MMIO\n");
        return false;
    }

    uintptr_t abar =
        (uintptr_t)(bar5 & 0xFFFFFFF0u);

    if (abar == 0)
    {
        printf("AHCI: invalid ABAR\n");
        return false;
    }

    printf(
        "AHCI: ABAR = 0x%x\n",
        (unsigned)abar);

    /*
     * Enable PCI memory-space access and bus mastering.
     */
    uint16_t command =
        pci_config_read16(
            device->bus,
            device->device,
            device->function,
            PCI_COMMAND_REGISTER);

    command |=
        PCI_COMMAND_MEMORY_SPACE |
        PCI_COMMAND_BUS_MASTER;

    pci_config_write16(
        device->bus,
        device->device,
        device->function,
        PCI_COMMAND_REGISTER,
        command);

    /*
     * Device MMIO should not be treated as normal cacheable RAM.
     */
    if (!paging_identity_map_range(
            abar,
            AHCI_MMIO_SIZE,
            PAGE_WRITABLE |
            PAGE_CACHE_DISABLE))
    {
        printf("AHCI: failed mapping ABAR\n");
        return false;
    }

    ahci_hba_header_t *hba =
        (ahci_hba_header_t *)abar;

    printf(
        "AHCI: CAP = 0x%x\n",
        (unsigned)hba->cap);

    printf(
        "AHCI: GHC = 0x%x\n",
        (unsigned)hba->ghc);

    printf(
        "AHCI: PI  = 0x%x\n",
        (unsigned)hba->pi);

    printf(
        "AHCI: VS  = 0x%x\n",
        (unsigned)hba->vs);

    return true;
}