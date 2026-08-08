#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/acpi.h>

/*
 * ==========================================================================
 * LEGACY PC ACPI SEARCH AREAS
 * ==========================================================================
 *
 * On BIOS systems:
 *
 *     1. Search first 1 KiB of EBDA
 *     2. Search BIOS area 0xE0000 - 0xFFFFF
 *
 * RSDP must be aligned on a 16-byte boundary.
 */
#define BIOS_DATA_EBDA_SEGMENT_ADDRESS  0x0000040Eu
#define EBDA_SEARCH_LENGTH              1024u
#define BIOS_SEARCH_START               0x000E0000u
#define BIOS_SEARCH_END                 0x00100000u
#define RSDP_ALIGNMENT                  16u

static const acpi_rsdp_t *root_rsdp = NULL;

/*
 * ==========================================================================
 * CHECKSUM
 * ==========================================================================
 *
 * ACPI table checksums are valid when the sum of all bytes wraps to zero.
 */
static bool acpi_checksum_valid(const void *data, size_t length)
{
    if (data == NULL) return false;

    const uint8_t *bytes = (const uint8_t *)data;

    uint8_t sum = 0;

    for (size_t i = 0; i < length; ++i)
        sum = (uint8_t)(sum + bytes[i]);

    return sum == 0;
}

/*
 * ==========================================================================
 * RSDP SEARCH
 * ==========================================================================
 */
static const acpi_rsdp_t* acpi_scan_rsdp_range(uintptr_t start, uintptr_t end)
{
    /*
     * Force the first address onto a 16-byte boundary.
     */
    start = (start + (RSDP_ALIGNMENT - 1u)) & ~(uintptr_t)(RSDP_ALIGNMENT - 1u);

    for (uintptr_t address = start; address + sizeof(acpi_rsdp_t) <= end; address += RSDP_ALIGNMENT)
    {
        const acpi_rsdp_t *rsdp = (const acpi_rsdp_t *)address;

        if (memcmp(rsdp->signature, "RSD PTR", 8u) != 0) continue;

        /*
         * ACPI 1.0 checksum covers the first 20 bytes.
         */
        if (!acpi_checksum_valid(rsdp, sizeof(acpi_rsdp_t))) continue;

        return rsdp;
    }

    return NULL;
}

/*
 * ==========================================================================
 * SDT VALIDATION
 * ==========================================================================
 */
static bool acpi_std_valid(const acpi_sdt_header_t *header)
{
    if (header == NULL) return false;

    /*
     * Every ACPI SDT must at least contain the common header.
     */
    if (header->length < sizeof(acpi_sdt_header_t)) return false;

    return acpi_checksum_valid(header, header->length);
}

/*
 * ==========================================================================
 * INITIALIZATION
 * ==========================================================================
 */
bool acpi_initialize(void)
{
    root_rsdp = NULL;

    /*
     * ----------------------------------------------------------------------
     * 1. Search EBDA
     * ----------------------------------------------------------------------
     *
     * BIOS Data Area offset 0x40E contains the EBDA segment.
     *
     * Convert segment to physical address by shifting left by 4.
     */

    const volatile uint16_t *ebda_segment_ptr = (const volatile uint16_t *)BIOS_DATA_EBDA_SEGMENT_ADDRESS;

    uint16_t ebda_segment = *ebda_segment_ptr;

    if (ebda_segment != 0) 
    {
        uintptr_t ebda_address = (uintptr_t)ebda_segment << 4;

        const acpi_rsdp_t *rsdp = acpi_scan_rsdp_range(ebda_address, ebda_address + EBDA_SEARCH_LENGTH);

        if (rsdp != NULL)
        {
            root_rsdp = rsdp;
            return true;
        }
    }

    /*
     * ----------------------------------------------------------------------
     * 2. Search BIOS ROM area
     * ----------------------------------------------------------------------
     */
    const acpi_rsdp_t *rsdp = acpi_scan_rsdp_range(BIOS_SEARCH_START, BIOS_SEARCH_END);

    if (rsdp == NULL) return false;

    root_rsdp = rsdp;

    return true;
}

/*
 * ==========================================================================
 * ACCESSORS
 * ==========================================================================
 */
const acpi_rsdp_t* acpi_get_rsdp(void)
{
    return root_rsdp;
}

/*
 * ==========================================================================
 * RSDT LOOKUP
 * ==========================================================================
 */
const acpi_sdt_header_t* acpi_find_table(const char signature[4])
{
    if (root_rsdp == NULL || signature == NULL) return NULL;

    /*
     * This first-stage i386 implementation uses RSDT.
     *
     * RSDT contains 32-bit physical pointers.
     */
    const acpi_sdt_header_t *rsdt = (const acpi_sdt_header_t*)(uintptr_t)root_rsdp->rsdt_address;

    if (!acpi_std_valid(rsdt)) return NULL;

    if (memcmp(rsdt->signature, "RSDT", 4u) != 0) return NULL;

    size_t payload_size = rsdt->length - sizeof(acpi_sdt_header_t);
    size_t entry_count = payload_size / sizeof(uint32_t);
    const uint32_t *entries = (const uint32_t*)((const uint8_t*)rsdt + sizeof(acpi_sdt_header_t));

    for (size_t i = 0; i < entry_count; ++i)
    {
        const acpi_sdt_header_t *table = (const acpi_sdt_header_t*)(uintptr_t)entries[i];
        
        if (!acpi_std_valid(table)) continue;

        if (memcmp(table->signature, signature, 4u) == 0) return table;
    }

    return NULL;
}
