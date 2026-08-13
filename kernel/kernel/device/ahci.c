#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <kernel/ahci.h>
#include <kernel/paging.h>
#include <kernel/block_device.h>
#include <kernel/pci.h>
#include <kernel/pmm.h>
#include <kernel/heap.h>

#define PCI_COMMAND_REGISTER 0x04u
#define PCI_BAR5 0x24u

#define PCI_COMMAND_MEMORY_SPACE 0x0002u
#define PCI_COMMAND_BUS_MASTER 0x0004u

#define AHCI_MMIO_SIZE 0x1100u

#define AHCI_PORT_DET_PRESENT 0x03u
#define AHCI_PORT_IPM_ACTIVE 0x01u

#define AHCI_SIG_ATA 0x00000101u
#define AHCI_SIG_ATAPI 0xEB140101u

#define AHCI_PORT_CMD_ST (1u << 0)
#define AHCI_PORT_CMD_FRE (1u << 4)
#define AHCI_PORT_CMD_FR (1u << 14)
#define AHCI_PORT_CMD_CR (1u << 15)

#define AHCI_COMMAND_LIST_SIZE 1024u
#define AHCI_RECEIVED_FIS_SIZE 256u

#define AHCI_ENGINE_TIMEOUT 1000000u

#define AHCI_FIS_TYPE_REG_H2D 0x27u

#define ATA_CMD_READ_DMA_EXT 0x25u

#define ATA_CMD_IDENTIFY_DEVICE 0xECu

#define AHCI_PORT_IS_TFES (1u << 30)

#define ATA_STATUS_ERR (1u << 0)
#define ATA_STATUS_DRQ (1u << 3)
#define ATA_STATUS_BSY (1u << 7)

#define AHCI_COMMAND_TIMEOUT 10000000u

#define AHCI_SECTOR_SIZE 512u

/*
 * One command table occupies one 4 KiB page.
 *
 * Fixed command-table area:
 *
 *   command FIS   64 bytes
 *   ATAPI command 16 bytes
 *   reserved      48 bytes
 *                 --------
 *                 128 bytes
 *
 * Remaining:
 *
 *   4096 - 128 = 3968 bytes
 *
 * Each PRDT entry is 16 bytes:
 *
 *   3968 / 16 = 248 entries
 */
#define AHCI_MAX_PRDT_ENTRIES 248u

/*
 * Keep the first block-device implementation intentionally bounded.
 *
 * 128 sectors * 512 bytes = 64 KiB per READ DMA EXT command.
 *
 * Even if the destination begins at the last byte of a page,
 * 64 KiB spans at most 17 virtual pages, so this is comfortably
 * below AHCI_MAX_PRDT_ENTRIES.
 */
#define AHCI_MAX_SECTORS_PER_COMMAND 128u

typedef volatile struct
{
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t reserved1[11];
    uint32_t vendor[4];
} ahci_port_t;

typedef volatile struct
{
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;

    uint8_t reserved[0xA0 - 0x2C];
    uint8_t vendor[0x100 - 0xA0];

    ahci_port_t ports[32];
} ahci_hba_t;

typedef struct __attribute__((packed))
{
    /*
     * DWORD 0:
     *
     * bits 0..4  = CFL, command FIS length in DWORDs
     * bit  5     = ATAPI
     * bit  6     = W, 1 = host-to-device write
     * bit  7     = prefetchable
     * ...
     */
    uint16_t flags;

    uint16_t prdt_length;

    volatile uint32_t prd_byte_count;

    uint32_t command_table_base;
    uint32_t command_table_base_upper;

    uint32_t reserved[4];
} ahci_command_header_t;

typedef struct __attribute__((packed))
{
    uint32_t data_base;
    uint32_t data_base_upper;
    uint32_t reserved;

    /*
     * bits 0..21 = byte count minus one
     * bit 31     = interrupt on completion
     */
    uint32_t byte_count_and_flags;
} ahci_prdt_entry_t;

typedef struct __attribute__((packed))
{
    uint8_t command_fis[64];
    uint8_t atapi_command[16];
    uint8_t reserved[48];

    ahci_prdt_entry_t prdt[AHCI_MAX_PRDT_ENTRIES];
} ahci_command_table_t;

typedef struct __attribute__((packed))
{
    uint8_t fis_type;

    uint8_t pmport_and_c;

    uint8_t command;
    uint8_t feature_low;

    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;

    uint8_t device;

    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;

    uint8_t feature_high;

    uint8_t count_low;
    uint8_t count_high;

    uint8_t icc;
    uint8_t control;

    uint8_t reserved[4];
} ahci_fis_reg_h2d_t;

static bool ahci_prepare_port(
    ahci_port_t *port,
    uint32_t port_index);
static void ahci_test_gpt_read(void);
static bool ahci_initialize_disk(void);

static ahci_port_t *ahci_disk_port = NULL;

static block_device_t ahci_disk;
static bool ahci_disk_present = false;

/*
 * One 64 KiB read-ahead window.
 *
 * The heap allocation may span physically non-contiguous pages.
 * That is fine because ahci_build_read_prdt() describes each
 * mapped page fragment separately.
 */
#define AHCI_READ_CACHE_SECTORS \
    AHCI_MAX_SECTORS_PER_COMMAND

#define AHCI_READ_CACHE_SIZE \
    (AHCI_READ_CACHE_SECTORS * AHCI_SECTOR_SIZE)

static uint8_t *ahci_read_cache = NULL;
static bool ahci_read_cache_valid = false;
static uint64_t ahci_read_cache_lba = 0;
static uint32_t ahci_read_cache_sector_count = 0;

static uintptr_t ahci_port_memory_physical = 0;
static void *ahci_port_memory_virtual = NULL;

static uintptr_t ahci_command_table_physical = 0;
static ahci_command_table_t *ahci_command_table = NULL;

static bool ahci_port_device_present(
    ahci_port_t *port)
{
    if (port == NULL)
        return false;

    uint32_t ssts = port->ssts;

    /*
     * PxSSTS:
     *
     * bits 0..3 = DET
     * bits 8..11 = IPM
     *
     * DET == 3:
     *   device present and PHY communication established
     *
     * IPM == 1:
     *   interface active
     */
    uint32_t det =
        ssts & 0x0Fu;

    uint32_t ipm =
        (ssts >> 8) & 0x0Fu;

    return det == AHCI_PORT_DET_PRESENT &&
           ipm == AHCI_PORT_IPM_ACTIVE;
}

static bool ahci_stop_command_engine(
    ahci_port_t *port)
{
    if (port == NULL)
        return false;

    /*
     * Stop accepting new commands.
     */
    port->cmd &= ~AHCI_PORT_CMD_ST;

    /*
     * Wait for the command-list engine to stop.
     */
    for (uint32_t timeout = 0;
         timeout < AHCI_ENGINE_TIMEOUT;
         timeout++)
    {
        if ((port->cmd & AHCI_PORT_CMD_CR) == 0)
            break;

        if (timeout == AHCI_ENGINE_TIMEOUT - 1u)
        {
            printf(
                "AHCI: timeout waiting for command engine\n");

            return false;
        }
    }

    /*
     * Stop FIS reception.
     */
    port->cmd &= ~AHCI_PORT_CMD_FRE;

    /*
     * Wait for the FIS receive engine to stop.
     */
    for (uint32_t timeout = 0;
         timeout < AHCI_ENGINE_TIMEOUT;
         timeout++)
    {
        if ((port->cmd & AHCI_PORT_CMD_FR) == 0)
            return true;
    }

    printf(
        "AHCI: timeout waiting for FIS receive engine\n");

    return false;
}

static bool ahci_start_command_engine(
    ahci_port_t *port)
{
    if (port == NULL)
        return false;

    /*
     * The engines must be stopped before restarting.
     */
    if ((port->cmd &
         (AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR)) != 0)
    {
        return false;
    }

    /*
     * FIS reception must be enabled before command processing.
     */
    port->cmd |= AHCI_PORT_CMD_FRE;
    port->cmd |= AHCI_PORT_CMD_ST;

    return true;
}

static void ahci_probe_ports(
    ahci_hba_t *hba)
{
    if (hba == NULL)
        return;

    uint32_t implemented =
        hba->pi;

    for (uint32_t port_index = 0;
         port_index < 32;
         port_index++)
    {
        /*
         * PI bit N tells us whether port N exists.
         */
        if ((implemented &
             (1u << port_index)) == 0)
        {
            continue;
        }

        ahci_port_t *port =
            &hba->ports[port_index];

        uint32_t ssts = port->ssts;
        uint32_t sig = port->sig;

        printf(
            "AHCI: port %u SSTS=0x%x SIG=0x%x\n",
            (unsigned)port_index,
            (unsigned)ssts,
            (unsigned)sig);

        if (!ahci_port_device_present(port))
            continue;

        if (sig == AHCI_SIG_ATA)
        {
            printf(
                "AHCI: SATA disk found on port %u\n",
                (unsigned)port_index);

            if (ahci_disk_port == NULL)
            {
                if (!ahci_prepare_port(
                        port,
                        port_index))
                {
                    printf(
                        "AHCI: failed preparing SATA port %u\n",
                        (unsigned)port_index);
                }
            }
        }
        else if (sig == AHCI_SIG_ATAPI)
        {
            printf(
                "AHCI: ATAPI device found on port %u\n",
                (unsigned)port_index);
        }
        else
        {
            printf(
                "AHCI: unknown device on port %u "
                "signature=0x%x\n",
                (unsigned)port_index,
                (unsigned)sig);
        }
    }
}

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

    ahci_hba_t *hba =
        (ahci_hba_t *)abar;

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

    ahci_probe_ports(hba);

    if (ahci_disk_port != NULL)
    {
        ahci_test_gpt_read();
        if (!ahci_initialize_disk())
        {
            printf(
                "AHCI: disk initialization failed\n");

            return false;
        }
    }

    return true;
}

static bool ahci_prepare_port(
    ahci_port_t *port,
    uint32_t port_index)
{
    if (port == NULL)
        return false;

    if (!ahci_stop_command_engine(port))
    {
        printf(
            "AHCI: failed stopping port %u\n",
            (unsigned)port_index);

        return false;
    }

    printf(
        "AHCI: port %u command engine stopped\n",
        (unsigned)port_index);

    /*
     * One physical page is enough for:
     *
     *   0x000 - 0x3FF : command list (1024 bytes)
     *   0x400 - 0x4FF : received FIS (256 bytes)
     */
    uintptr_t frame =
        pmm_allocate_frame();

    if (frame == 0)
    {
        printf(
            "AHCI: failed allocating port memory\n");

        return false;
    }

    /*
     * The controller accesses physical memory directly.
     *
     * We identity-map this physical frame so the CPU can also
     * initialize and use it.
     */
    if (!paging_identity_map_range(
            frame,
            PAGE_SIZE,
            PAGE_WRITABLE))
    {
        pmm_free_frame(frame);

        printf(
            "AHCI: failed mapping port memory\n");

        return false;
    }

    void *memory =
        (void *)frame;

    memset(
        memory,
        0,
        PAGE_SIZE);

    uintptr_t command_list_physical =
        frame;

    uintptr_t received_fis_physical =
        frame + AHCI_COMMAND_LIST_SIZE;

    uintptr_t command_table_frame =
        pmm_allocate_frame();

    if (command_table_frame == 0)
    {
        printf(
            "AHCI: failed allocating command table\n");

        return false;
    }

    if (!paging_identity_map_range(
            command_table_frame,
            PAGE_SIZE,
            PAGE_WRITABLE))
    {
        pmm_free_frame(command_table_frame);

        printf(
            "AHCI: failed mapping command table\n");

        return false;
    }

    memset(
        (void *)command_table_frame,
        0,
        PAGE_SIZE);

    /*
     * We're running a 32-bit kernel and these PMM frames are below
     * 4 GiB, so the upper address registers are zero.
     */
    port->clb =
        (uint32_t)command_list_physical;

    port->clbu = 0;

    port->fb =
        (uint32_t)received_fis_physical;

    port->fbu = 0;

    /*
     * Configure command-list slot 0.
     */
    ahci_command_header_t *command_list =
        (ahci_command_header_t *)memory;

    ahci_command_header_t *header =
        &command_list[0];

    memset(
        header,
        0,
        sizeof(*header));

    header->command_table_base =
        (uint32_t)command_table_frame;

    header->command_table_base_upper = 0;

    /*
     * Clear pending status before starting the engine.
     *
     * PxIS and PxSERR use write-1-to-clear semantics.
     */
    port->is = 0xFFFFFFFFu;
    port->serr = 0xFFFFFFFFu;

    /*
     * Keep interrupts disabled for now.
     *
     * Our first commands will be polling based.
     */
    port->ie = 0;

    if (!ahci_start_command_engine(port))
    {
        printf(
            "AHCI: failed starting port %u\n",
            (unsigned)port_index);

        return false;
    }

    ahci_disk_port = port;

    ahci_port_memory_physical = frame;
    ahci_port_memory_virtual = memory;
    ahci_command_table_physical = command_table_frame;
    ahci_command_table = (ahci_command_table_t *)command_table_frame;

    printf(
        "AHCI: command list phys=0x%x\n",
        (unsigned)command_list_physical);

    printf(
        "AHCI: received FIS phys=0x%x\n",
        (unsigned)received_fis_physical);

    printf(
        "AHCI: port %u command engine started\n",
        (unsigned)port_index);

    printf(
        "AHCI: command table phys=0x%x\n",
        (unsigned)command_table_frame);

    return true;
}

static bool ahci_wait_device_ready(
    ahci_port_t *port)
{
    if (port == NULL)
        return false;

    for (uint32_t timeout = 0;
         timeout < AHCI_COMMAND_TIMEOUT;
         timeout++)
    {
        uint8_t status =
            (uint8_t)(port->tfd & 0xFFu);

        if ((status &
             (ATA_STATUS_BSY |
              ATA_STATUS_DRQ)) == 0)
        {
            return true;
        }
    }

    printf(
        "AHCI: timeout waiting for device ready\n");

    return false;
}

static bool ahci_build_read_prdt(
    void *buffer,
    size_t byte_count,
    uint16_t *prdt_count)
{
    if (buffer == NULL ||
        byte_count == 0 ||
        prdt_count == NULL ||
        ahci_command_table == NULL)
    {
        return false;
    }

    uintptr_t virtual_address =
        (uintptr_t)buffer;

    size_t remaining =
        byte_count;

    uint16_t entry_index = 0;

    while (remaining > 0)
    {
        if (entry_index >= AHCI_MAX_PRDT_ENTRIES)
        {
            printf(
                "AHCI: too many PRDT entries\n");

            return false;
        }

        uintptr_t physical_address;

        if (!paging_get_physical_address(
                virtual_address,
                &physical_address))
        {
            printf(
                "AHCI: failed translating DMA buffer "
                "at virtual=0x%x\n",
                (unsigned)virtual_address);

            return false;
        }

        /*
         * A virtual page maps to one physical frame.
         *
         * We may therefore safely describe bytes only up to the
         * end of this virtual page with this physical address.
         */
        size_t page_offset =
            virtual_address &
            (PAGE_SIZE - 1u);

        size_t page_bytes =
            PAGE_SIZE - page_offset;

        size_t fragment_bytes =
            remaining < page_bytes
                ? remaining
                : page_bytes;

        ahci_prdt_entry_t *entry =
            &ahci_command_table->prdt[entry_index];

        entry->data_base =
            (uint32_t)physical_address;

        /*
         * Current 32-bit kernel physical memory is below 4 GiB.
         */
        entry->data_base_upper = 0;

        entry->reserved = 0;

        /*
         * AHCI PRDT DBC contains byte-count-minus-one.
         *
         * IOC remains zero because this driver is polling.
         */
        entry->byte_count_and_flags =
            (uint32_t)(fragment_bytes - 1u);

        virtual_address +=
            fragment_bytes;

        remaining -=
            fragment_bytes;

        entry_index++;
    }

    *prdt_count =
        entry_index;

    return true;
}

static bool ahci_read_dma(
    uint64_t lba,
    uint32_t sector_count,
    void *buffer)
{
    if (ahci_disk_port == NULL ||
        ahci_command_table == NULL ||
        buffer == NULL)
    {
        return false;
    }

    if (sector_count == 0 ||
        sector_count >
            AHCI_MAX_SECTORS_PER_COMMAND)
    {
        return false;
    }

    /*
     * READ DMA EXT uses a 48-bit LBA.
     */
    const uint64_t lba48_limit =
        (1ULL << 48);

    if (lba >= lba48_limit)
        return false;

    if ((uint64_t)sector_count >
        lba48_limit - lba)
    {
        return false;
    }

    size_t byte_count =
        (size_t)sector_count *
        AHCI_SECTOR_SIZE;

    ahci_port_t *port =
        ahci_disk_port;

    /*
     * Slot zero must not already be active.
     */
    if ((port->ci & 1u) != 0 ||
        (port->sact & 1u) != 0)
    {
        printf(
            "AHCI: command slot 0 busy\n");

        return false;
    }

    if (!ahci_wait_device_ready(port))
        return false;

    /*
     * Clear stale interrupt/error state.
     */
    port->is =
        0xFFFFFFFFu;

    /*
     * The command-table page contains the FIS and all PRDT
     * entries for this command.
     */
    memset(
        ahci_command_table,
        0,
        PAGE_SIZE);

    uint16_t prdt_count = 0;

    if (!ahci_build_read_prdt(
            buffer,
            byte_count,
            &prdt_count))
    {
        return false;
    }

    ahci_command_header_t *command_list =
        (ahci_command_header_t *)
            ahci_port_memory_virtual;

    ahci_command_header_t *header =
        &command_list[0];

    /*
     * Preserve command-table physical address while resetting
     * command-specific header fields.
     */
    uint32_t command_table_base =
        header->command_table_base;

    uint32_t command_table_base_upper =
        header->command_table_base_upper;

    memset(
        header,
        0,
        sizeof(*header));

    header->command_table_base =
        command_table_base;

    header->command_table_base_upper =
        command_table_base_upper;

    /*
     * Register H2D FIS is 20 bytes = 5 DWORDs.
     *
     * W remains zero: disk -> memory.
     */
    header->flags =
        5u;

    header->prdt_length =
        prdt_count;

    ahci_fis_reg_h2d_t *fis =
        (ahci_fis_reg_h2d_t *)
            ahci_command_table->command_fis;

    memset(
        fis,
        0,
        sizeof(*fis));

    fis->fis_type =
        AHCI_FIS_TYPE_REG_H2D;

    /*
     * C = 1: this FIS contains a command.
     */
    fis->pmport_and_c =
        1u << 7;

    fis->command =
        ATA_CMD_READ_DMA_EXT;

    /*
     * 48-bit LBA.
     */
    fis->lba0 =
        (uint8_t)(lba >> 0);

    fis->lba1 =
        (uint8_t)(lba >> 8);

    fis->lba2 =
        (uint8_t)(lba >> 16);

    fis->lba3 =
        (uint8_t)(lba >> 24);

    fis->lba4 =
        (uint8_t)(lba >> 32);

    fis->lba5 =
        (uint8_t)(lba >> 40);

    /*
     * LBA addressing mode.
     */
    fis->device =
        1u << 6;

    /*
     * READ DMA EXT has a 16-bit sector-count field.
     *
     * This milestone limits commands to 128 sectors, so zero
     * never needs the ATA 65536-sector special meaning.
     */
    fis->count_low =
        (uint8_t)(sector_count &
                  0xFFu);

    fis->count_high =
        (uint8_t)((sector_count >> 8) &
                  0xFFu);

    /*
     * Issue command-list slot zero.
     */
    port->ci =
        1u;

    for (uint32_t timeout = 0;
         timeout < AHCI_COMMAND_TIMEOUT;
         timeout++)
    {
        if ((port->is &
             AHCI_PORT_IS_TFES) != 0)
        {
            printf(
                "AHCI: task file error "
                "IS=0x%x TFD=0x%x\n",
                (unsigned)port->is,
                (unsigned)port->tfd);

            return false;
        }

        if ((port->ci & 1u) == 0)
        {
            uint8_t status =
                (uint8_t)(port->tfd &
                          0xFFu);

            if ((status &
                 ATA_STATUS_ERR) != 0)
            {
                printf(
                    "AHCI: ATA error after DMA read "
                    "TFD=0x%x\n",
                    (unsigned)port->tfd);

                return false;
            }

            return true;
        }
    }

    printf(
        "AHCI: DMA read timeout "
        "CI=0x%x IS=0x%x TFD=0x%x\n",
        (unsigned)port->ci,
        (unsigned)port->is,
        (unsigned)port->tfd);

    return false;
}

static bool ahci_read_cache_contains(
    uint64_t lba,
    uint32_t count)
{
    if (!ahci_read_cache_valid ||
        ahci_read_cache == NULL ||
        count == 0)
    {
        return false;
    }

    if (lba < ahci_read_cache_lba)
        return false;

    uint64_t offset =
        lba - ahci_read_cache_lba;

    if (offset >=
        ahci_read_cache_sector_count)
    {
        return false;
    }

    uint32_t offset_sectors =
        (uint32_t)offset;

    return count <=
           ahci_read_cache_sector_count -
               offset_sectors;
}

static int ahci_block_read(
    block_device_t *device,
    uint64_t lba,
    uint32_t count,
    void *buffer)
{
    if (device == NULL ||
        buffer == NULL ||
        count == 0)
    {
        return -1;
    }

    if (lba >=
        device->sector_count)
    {
        return -1;
    }

    if ((uint64_t)count >
        device->sector_count - lba)
    {
        return -1;
    }

    /*
     * Small reads use the read-ahead cache.
     *
     * This is the important case for the current ext2 driver:
     * one 4 KiB filesystem block = 8 disk sectors.
     */
    if (count <= AHCI_READ_CACHE_SECTORS &&
        ahci_read_cache != NULL)
    {
        /*
         * Fast path: requested sectors are already inside
         * the current 64 KiB window.
         */
        if (!ahci_read_cache_contains(
                lba,
                count))
        {
            /*
             * Cache miss.
             *
             * Instead of reading only what the caller requested,
             * read up to 128 sectors starting at this LBA.
             */
            uint64_t sectors_available =
                device->sector_count - lba;

            uint32_t read_ahead_count =
                sectors_available >
                        AHCI_READ_CACHE_SECTORS
                    ? AHCI_READ_CACHE_SECTORS
                    : (uint32_t)sectors_available;

            /*
             * Mark invalid before DMA so a failed command never
             * leaves stale data advertised as valid.
             */
            ahci_read_cache_valid =
                false;

            ahci_read_cache_sector_count =
                0;

            if (!ahci_read_dma(
                    lba,
                    read_ahead_count,
                    ahci_read_cache))
            {
                return -1;
            }

            ahci_read_cache_lba =
                lba;

            ahci_read_cache_sector_count =
                read_ahead_count;

            ahci_read_cache_valid =
                true;
        }

        /*
         * The requested range must now be inside the cache.
         */
        if (!ahci_read_cache_contains(
                lba,
                count))
        {
            return -1;
        }

        uint64_t sector_offset =
            lba - ahci_read_cache_lba;

        size_t byte_offset =
            (size_t)sector_offset *
            AHCI_SECTOR_SIZE;

        size_t byte_count =
            (size_t)count *
            AHCI_SECTOR_SIZE;

        memcpy(
            buffer,
            ahci_read_cache + byte_offset,
            byte_count);

        return 0;
    }

    /*
     * Large requests bypass read-ahead and use the existing
     * multi-sector DMA path directly.
     */
    uint64_t current_lba =
        lba;

    uint32_t remaining =
        count;

    uint8_t *destination =
        (uint8_t *)buffer;

    while (remaining > 0)
    {
        uint32_t chunk =
            remaining >
                    AHCI_MAX_SECTORS_PER_COMMAND
                ? AHCI_MAX_SECTORS_PER_COMMAND
                : remaining;

        if (!ahci_read_dma(
                current_lba,
                chunk,
                destination))
        {
            return -1;
        }

        current_lba +=
            chunk;

        destination +=
            (size_t)chunk *
            AHCI_SECTOR_SIZE;

        remaining -=
            chunk;
    }

    return 0;
}

static bool ahci_identify_device(
    uint16_t identify[256])
{
    if (ahci_disk_port == NULL ||
        ahci_command_table == NULL ||
        identify == NULL)
    {
        return false;
    }

    uintptr_t buffer_frame =
        pmm_allocate_frame();

    if (buffer_frame == 0)
    {
        printf("AHCI: IDENTIFY buffer allocation failed\n");
        return false;
    }

    if (!paging_identity_map_range(
            buffer_frame,
            PAGE_SIZE,
            PAGE_WRITABLE))
    {
        pmm_free_frame(buffer_frame);

        printf("AHCI: IDENTIFY buffer mapping failed\n");
        return false;
    }

    memset(
        (void *)buffer_frame,
        0,
        PAGE_SIZE);

    ahci_port_t *port =
        ahci_disk_port;

    if ((port->ci & 1u) != 0 ||
        (port->sact & 1u) != 0)
    {
        printf("AHCI: slot 0 busy during IDENTIFY\n");
        return false;
    }

    if (!ahci_wait_device_ready(port))
        return false;

    port->is = 0xFFFFFFFFu;

    memset(
        ahci_command_table,
        0,
        PAGE_SIZE);

    ahci_command_header_t *command_list =
        (ahci_command_header_t *)
            ahci_port_memory_virtual;

    ahci_command_header_t *header =
        &command_list[0];

    uint32_t ctba =
        header->command_table_base;

    uint32_t ctbau =
        header->command_table_base_upper;

    memset(
        header,
        0,
        sizeof(*header));

    header->command_table_base = ctba;
    header->command_table_base_upper = ctbau;

    /*
     * Register H2D FIS:
     * 20 bytes = 5 DWORDs.
     */
    header->flags = 5u;

    /*
     * One 512-byte receive buffer.
     */
    header->prdt_length = 1;

    ahci_prdt_entry_t *prdt =
        &ahci_command_table->prdt[0];

    prdt->data_base =
        (uint32_t)buffer_frame;

    prdt->data_base_upper = 0;

    prdt->reserved = 0;

    prdt->byte_count_and_flags =
        AHCI_SECTOR_SIZE - 1u;

    ahci_fis_reg_h2d_t *fis =
        (ahci_fis_reg_h2d_t *)
            ahci_command_table->command_fis;

    memset(
        fis,
        0,
        sizeof(*fis));

    fis->fis_type =
        AHCI_FIS_TYPE_REG_H2D;

    /*
     * C bit:
     * this is a command FIS.
     */
    fis->pmport_and_c =
        1u << 7;

    fis->command =
        ATA_CMD_IDENTIFY_DEVICE;

    /*
     * IDENTIFY DEVICE does not use an LBA or sector count.
     */

    port->ci = 1u;

    for (uint32_t timeout = 0;
         timeout < AHCI_COMMAND_TIMEOUT;
         timeout++)
    {
        if ((port->is &
             AHCI_PORT_IS_TFES) != 0)
        {
            printf(
                "AHCI: IDENTIFY task-file error "
                "IS=0x%x TFD=0x%x\n",
                (unsigned)port->is,
                (unsigned)port->tfd);

            return false;
        }

        if ((port->ci & 1u) == 0)
        {
            uint8_t status =
                (uint8_t)(port->tfd & 0xFFu);

            if ((status & ATA_STATUS_ERR) != 0)
            {
                printf(
                    "AHCI: IDENTIFY ATA error "
                    "TFD=0x%x\n",
                    (unsigned)port->tfd);

                return false;
            }

            memcpy(
                identify,
                (void *)buffer_frame,
                AHCI_SECTOR_SIZE);

            return true;
        }
    }

    printf(
        "AHCI: IDENTIFY timeout "
        "CI=0x%x IS=0x%x TFD=0x%x\n",
        (unsigned)port->ci,
        (unsigned)port->is,
        (unsigned)port->tfd);

    return false;
}

static uint64_t ahci_identify_sector_count(
    const uint16_t identify[256])
{
    /*
     * Word 83 bit 10 indicates 48-bit LBA support.
     */
    bool supports_lba48 =
        (identify[83] & (1u << 10)) != 0;

    if (supports_lba48)
    {
        uint64_t sectors =
            ((uint64_t)identify[100]) |
            ((uint64_t)identify[101] << 16) |
            ((uint64_t)identify[102] << 32) |
            ((uint64_t)identify[103] << 48);

        if (sectors != 0)
            return sectors;
    }

    /*
     * Fall back to the legacy 28-bit sector count.
     */
    return ((uint32_t)identify[61] << 16) |
           (uint32_t)identify[60];
}

static void ahci_identify_model(
    const uint16_t identify[256],
    char model[41])
{
    for (size_t i = 0; i < 20; i++)
    {
        uint16_t word =
            identify[27 + i];

        model[i * 2] =
            (char)(word >> 8);

        model[i * 2 + 1] =
            (char)(word & 0xFF);
    }

    model[40] = '\0';

    for (int i = 39; i >= 0; i--)
    {
        if (model[i] == ' ')
            model[i] = '\0';
        else
            break;
    }
}

static bool ahci_initialize_disk(void)
{
    uint16_t identify[256];

    memset(
        identify,
        0,
        sizeof(identify));

    ahci_disk_present =
        false;

    if (!ahci_identify_device(
            identify))
    {
        printf(
            "AHCI: IDENTIFY failed\n");

        return false;
    }

    uint64_t sector_count =
        ahci_identify_sector_count(
            identify);

    if (sector_count == 0)
    {
        printf(
            "AHCI: invalid sector count\n");

        return false;
    }

    char model[41];

    ahci_identify_model(
        identify,
        model);

    ahci_disk.name =
        "ahci0";

    ahci_disk.sector_size =
        AHCI_SECTOR_SIZE;

    ahci_disk.sector_count =
        sector_count;

    ahci_disk.read =
        ahci_block_read;

    /*
     * Read-only for now.
     */
    ahci_disk.write =
        NULL;

    ahci_disk.private_data =
        NULL;

    /*
     * Allocate one persistent 64 KiB read-ahead buffer.
     *
     * kmalloc() maps ordinary kernel heap pages. They do not have
     * to be physically contiguous because our PRDT builder translates
     * each page fragment independently.
     */
    if (ahci_read_cache == NULL)
    {
        ahci_read_cache =
            kmalloc(AHCI_READ_CACHE_SIZE);

        if (ahci_read_cache == NULL)
        {
            printf(
                "AHCI: failed allocating read cache\n");

            return false;
        }
    }

    ahci_read_cache_valid =
        false;

    ahci_read_cache_lba =
        0;

    ahci_read_cache_sector_count =
        0;

    printf(
        "AHCI: read-ahead cache: %u KiB\n",
        (unsigned)(AHCI_READ_CACHE_SIZE /
                   1024u));

    ahci_disk_present =
        true;

    printf(
        "AHCI: block device %s ready\n",
        ahci_disk.name);

    printf(
        "AHCI: model: %s\n",
        model);

    printf(
        "AHCI: sectors: %u\n",
        (unsigned)sector_count);

    printf(
        "AHCI: capacity: %u MiB\n",
        (unsigned)(sector_count *
                   AHCI_SECTOR_SIZE /
                   (1024u * 1024u)));

    return true;
}

static void ahci_test_gpt_read(void)
{
    if (ahci_disk_port == NULL)
        return;

    uintptr_t frame =
        pmm_allocate_frame();

    if (frame == 0)
    {
        printf(
            "AHCI: failed allocating test buffer\n");

        return;
    }

    if (!paging_identity_map_range(
            frame,
            PAGE_SIZE,
            PAGE_WRITABLE))
    {
        pmm_free_frame(frame);

        printf(
            "AHCI: failed mapping test buffer\n");

        return;
    }

    uint8_t *buffer =
        (uint8_t *)frame;

    memset(
        buffer,
        0,
        PAGE_SIZE);

    if (!ahci_read_dma(
            1,
            1,
            buffer))
    {
        printf(
            "AHCI: DMA read LBA 1 failed\n");

        return;
    }

    printf(
        "AHCI: DMA read LBA 1 succeeded\n");

    printf(
        "AHCI: GPT signature = "
        "%c%c%c%c%c%c%c%c\n",
        buffer[0],
        buffer[1],
        buffer[2],
        buffer[3],
        buffer[4],
        buffer[5],
        buffer[6],
        buffer[7]);
}

block_device_t *ahci_primary_disk(void)
{
    if (!ahci_disk_present)
        return NULL;

    return &ahci_disk;
}