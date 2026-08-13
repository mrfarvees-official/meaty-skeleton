#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <kernel/ahci.h>
#include <kernel/paging.h>
#include <kernel/pci.h>
#include <kernel/pmm.h>

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

static bool ahci_prepare_port(
    ahci_port_t *port,
    uint32_t port_index);
    
static ahci_port_t *ahci_disk_port = NULL;

static uintptr_t ahci_port_memory_physical = 0;
static void *ahci_port_memory_virtual = NULL;

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

    printf(
        "AHCI: command list phys=0x%x\n",
        (unsigned)command_list_physical);

    printf(
        "AHCI: received FIS phys=0x%x\n",
        (unsigned)received_fis_physical);

    printf(
        "AHCI: port %u command engine started\n",
        (unsigned)port_index);

    return true;
}