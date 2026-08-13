#ifndef KERNEL_PCI_H
#define KERNEL_PCI_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  header_type;
} pci_device_t;

uint32_t pci_config_read32(
    uint8_t bus, 
    uint8_t device, 
    uint8_t function, 
    uint8_t offset);
    
uint16_t pci_config_read16(
    uint8_t bus, 
    uint8_t device, 
    uint8_t function, 
    uint8_t offset);

uint8_t  pci_config_read8(
    uint8_t bus, 
    uint8_t device, 
    uint8_t function, 
    uint8_t offset);

void pci_config_write32(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset,
    uint32_t value);

void pci_config_write16(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset,
    uint16_t value);

bool pci_find_ahci_controller(pci_device_t *result);

#endif