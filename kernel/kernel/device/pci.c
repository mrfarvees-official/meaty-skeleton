#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

#include <kernel/pci.h>

#include "../../arch/i386/io.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_VENDOR_NONE    0xFFFFu

/*
 * PCI mass-storage controller:
 *
 * class    = 0x01
 * subclass = 0x06   SATA
 * prog_if  = 0x01   AHCI 1.0
 */
#define PCI_CLASS_MASS_STORAGE 0x01u
#define PCI_SUBCLASS_SATA      0x06u
#define PCI_PROGIF_AHCI        0x01u

static uint32_t pci_make_address(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset)
{
    return
        (1u << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFCu);
}

uint32_t pci_config_read32(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset)
{
    uint32_t address =
        pci_make_address(
            bus,
            device,
            function,
            offset);

    outl(PCI_CONFIG_ADDRESS, address);

    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset)
{
    uint32_t value =
        pci_config_read32(
            bus,
            device,
            function,
            offset);

    uint32_t shift =
        (uint32_t)(offset & 2u) * 8u;

    return (uint16_t)((value >> shift) & 0xFFFFu);
}

uint8_t pci_config_read8(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset)
{
    uint32_t value =
        pci_config_read32(
            bus,
            device,
            function,
            offset);

    uint32_t shift =
        (uint32_t)(offset & 3u) * 8u;

    return (uint8_t)((value >> shift) & 0xFFu);
}

static bool pci_read_device(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    pci_device_t *result)
{
    if (result == NULL)
        return false;

    uint16_t vendor_id =
        pci_config_read16(
            bus,
            device,
            function,
            0x00);

    if (vendor_id == PCI_VENDOR_NONE)
        return false;

    result->bus = bus;
    result->device = device;
    result->function = function;

    result->vendor_id = vendor_id;

    result->device_id =
        pci_config_read16(
            bus,
            device,
            function,
            0x02);

    result->prog_if =
        pci_config_read8(
            bus,
            device,
            function,
            0x09);

    result->subclass =
        pci_config_read8(
            bus,
            device,
            function,
            0x0A);

    result->class_code =
        pci_config_read8(
            bus,
            device,
            function,
            0x0B);

    result->header_type =
        pci_config_read8(
            bus,
            device,
            function,
            0x0E);

    return true;
}

bool pci_find_ahci_controller(pci_device_t *result)
{
    if (result == NULL)
        return false;

    for (uint32_t bus = 0; bus < 256; bus++)
    {
        for (uint32_t device = 0; device < 32; device++)
        {
            /*
             * Read function 0 first.
             */
            pci_device_t first;

            if (!pci_read_device(
                    (uint8_t)bus,
                    (uint8_t)device,
                    0,
                    &first))
            {
                continue;
            }

            uint32_t function_count =
                (first.header_type & 0x80u)
                    ? 8u
                    : 1u;

            for (uint32_t function = 0;
                 function < function_count;
                 function++)
            {
                pci_device_t current;

                if (!pci_read_device(
                        (uint8_t)bus,
                        (uint8_t)device,
                        (uint8_t)function,
                        &current))
                {
                    continue;
                }

                if (current.class_code ==
                        PCI_CLASS_MASS_STORAGE &&
                    current.subclass ==
                        PCI_SUBCLASS_SATA &&
                    current.prog_if ==
                        PCI_PROGIF_AHCI)
                {
                    *result = current;

                    return true;
                }
            }
        }
    }

    return false;
}

void pci_config_write32(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset,
    uint32_t value)
{
    uint32_t address =
        pci_make_address(
            bus,
            device,
            function,
            offset);

    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

void pci_config_write16(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset,
    uint16_t value)
{
    uint8_t aligned_offset =
        offset & 0xFCu;

    uint32_t current =
        pci_config_read32(
            bus,
            device,
            function,
            aligned_offset);

    uint32_t shift =
        (uint32_t)(offset & 2u) * 8u;

    current &=
        ~(0xFFFFu << shift);

    current |=
        (uint32_t)value << shift;

    pci_config_write32(
        bus,
        device,
        function,
        aligned_offset,
        current);
}