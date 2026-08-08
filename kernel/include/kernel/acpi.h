#ifndef KERNEL_ACPI_H
#define KERNEL_ACPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * ==========================================================================
 * ACPI SDT HEADER
 * ==========================================================================
 */
typedef struct acpi_sdt_header
{
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

/*
 * ==========================================================================
 * RSDP
 * ==========================================================================
 *
 * ACPI 1.0 portion.
 *
 * For this first i386 stage we intentionally use the RSDT, whose address
 * is 32-bit.
 */
typedef struct acpi_rsdp
{
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed)) acpi_rsdp_t;

/*
 * Search legacy BIOS memory for the Root System Description Pointer.
 */
bool acpi_initialize(void);

/*
 * Return the discovered RSDP.
 */
const acpi_rsdp_t* acpi_get_rsdp(void);

/*
 * Find an ACPI table by its four-character signature.
 *
 * Example:
 *
 *     acpi_find_table("APIC")
 */
const acpi_sdt_header_t* acpi_find_table(const char signature[4]);

#endif