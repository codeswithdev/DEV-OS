/*
 * DevOS — AHCI (Advanced Host Controller Interface) driver
 *
 * Detects AHCI controller via PCI class 0x01/subclass 0x06.
 * Maps ABAR (BAR5) and initialises HBA + active ports.
 * Implements SATA read/write via FIS-based DMA (physical region descriptor).
 *
 * AHCI memory map (offsets from ABAR):
 *   0x000  GHC  (Global Host Control)
 *   0x00C  PI   (Ports Implemented bitmask)
 *   0x004  GHC (AHCI enable bit 31, reset bit 0)
 *   Port n: ABAR + 0x100 + n*0x80
 *     +0x00  CLB  Command List Base Address (low)
 *     +0x04  CLBU (high)
 *     +0x08  FB   FIS Base Address (low)
 *     +0x0C  FBU  (high)
 *     +0x10  IS   Interrupt Status
 *     +0x18  CMD  Port Command & Status
 *     +0x24  TFD  Task File Data
 *     +0x28  SIG  Signature
 *     +0x2C  SSTS SStatus (link detect)
 *     +0x34  SCTL SControl
 *     +0x38  SERR SError
 */
#include "ahci.h"
#include "block.h"
#include "../pci/pci.h"
#include "../../mm/heap.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../arch/x86_64/serial.h"
#include "../../lib/string.h"
#include "../../lib/printf.h"

/* ---- AHCI register layout ---- */

#define AHCI_GHC_AE         (1U << 31)  /* AHCI Enable */
#define AHCI_GHC_HR         (1U << 0)   /* HBA Reset */

#define AHCI_PORT_CMD_ST    (1U << 0)   /* Start */
#define AHCI_PORT_CMD_FRE   (1U << 4)   /* FIS Receive Enable */
#define AHCI_PORT_CMD_FR    (1U << 14)  /* FIS Receive Running */
#define AHCI_PORT_CMD_CR    (1U << 15)  /* Command List Running */

#define AHCI_SSTS_DET_MASK  0x0F
#define AHCI_SSTS_DET_PRES  3           /* Device Present + comms established */

#define AHCI_SIG_SATA       0x00000101  /* SATA drive */
#define AHCI_SIG_SATAPI     0xEB140101  /* SATAPI drive */

#define AHCI_TFD_ERR        (1 << 0)
#define AHCI_TFD_DRQ        (1 << 3)
#define AHCI_TFD_BSY        (1 << 7)

#define FIS_TYPE_REG_H2D    0x27
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_DMA_READ    0xC8
#define ATA_CMD_DMA_WRITE   0xCA
#define ATA_CMD_IDENTIFY_PACKET 0xA1

/* Command list entry (32 bytes) */
typedef struct {
    uint16_t cfl  : 5;     /* Command FIS length in DWORDs */
    uint16_t a    : 1;     /* ATAPI */
    uint16_t w    : 1;     /* Write (1=D2H→H2D) */
    uint16_t p    : 1;     /* Prefetchable */
    uint16_t r    : 1;     /* Reset */
    uint16_t b    : 1;     /* BIST */
    uint16_t c    : 1;     /* Clear busy upon R_OK */
    uint16_t rsv0 : 1;
    uint16_t pmp  : 4;     /* Port Multiplier Port */
    uint16_t prdtl;        /* Physical region descriptor table length */
    volatile uint32_t prdbc;  /* PRDT byte count transferred */
    uint32_t ctba;         /* Command Table Descriptor Base Address (low) */
    uint32_t ctbau;        /* (high) */
    uint32_t rsv[4];
} PACKED ahci_cmd_hdr_t;

/* Physical Region Descriptor Table Entry (16 bytes) */
typedef struct {
    uint32_t dba;          /* Data Base Address (low) */
    uint32_t dbau;         /* (high) */
    uint32_t rsv;
    uint32_t dbc : 22;     /* Byte count - 1 */
    uint32_t rsv2 : 9;
    uint32_t i   : 1;      /* Interrupt on completion */
} PACKED ahci_prd_t;

/* H2D Register FIS */
typedef struct {
    uint8_t  fis_type;     /* FIS_TYPE_REG_H2D */
    uint8_t  pmport : 4;
    uint8_t  rsv0   : 3;
    uint8_t  c      : 1;   /* 1 = Command, 0 = Control */
    uint8_t  command;
    uint8_t  featurel;
    uint8_t  lba0, lba1, lba2;
    uint8_t  device;
    uint8_t  lba3, lba4, lba5;
    uint8_t  featureh;
    uint16_t count;
    uint8_t  icc;
    uint8_t  control;
    uint32_t rsv1;
} PACKED ahci_fis_h2d_t;

/* Command table (variable length; we allocate for 1 PRD) */
typedef struct {
    uint8_t     cfis[64];   /* Command FIS */
    uint8_t     acmd[16];   /* ATAPI command */
    uint8_t     rsv[48];
    ahci_prd_t  prd[1];     /* 1 PRD entry */
} PACKED ahci_cmd_table_t;

/* Per-port volatile registers (MMIO view) */
typedef volatile struct {
    uint32_t clb, clbu;
    uint32_t fb, fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t rsv0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t rsv1[11];
    uint32_t vendor[4];
} PACKED ahci_port_regs_t;

/* HBA generic host control registers */
typedef volatile struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_ports;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t  rsv[0xA0 - 0x2C];
    uint8_t  vendor[0x100 - 0xA0];
    ahci_port_regs_t ports[32];
} PACKED ahci_hba_t;

/* ---- Driver state ---- */

#define AHCI_MAX_PORTS 32

typedef struct {
    int                 port_num;
    bool                present;
    bool                is_atapi;
    char                model[41];
    uint64_t            sector_count;
    ahci_port_regs_t   *regs;
    ahci_cmd_hdr_t     *cmd_list;   /* phys-aligned, kmalloc'd */
    ahci_cmd_table_t   *cmd_table;
    uint8_t            *fis_buf;
    block_device_t      blkdev;
} ahci_port_t;

static ahci_hba_t *hba = NULL;
static ahci_port_t ports[AHCI_MAX_PORTS];
static int         port_count = 0;

/* ---- Port start/stop ---- */

static void port_stop(ahci_port_regs_t *p)
{
    p->cmd &= ~AHCI_PORT_CMD_ST;
    uint32_t n = 500000;
    while ((p->cmd & AHCI_PORT_CMD_CR) && n--);
    p->cmd &= ~AHCI_PORT_CMD_FRE;
    n = 500000;
    while ((p->cmd & AHCI_PORT_CMD_FR) && n--);
}

static void port_start(ahci_port_regs_t *p)
{
    while (p->cmd & AHCI_PORT_CMD_CR);
    p->cmd |= AHCI_PORT_CMD_FRE;
    p->cmd |= AHCI_PORT_CMD_ST;
}

/* ---- Port initialization ---- */

static bool port_init(int pnum)
{
    ahci_port_regs_t *p = &hba->ports[pnum];

    /* Check presence */
    if ((p->ssts & AHCI_SSTS_DET_MASK) != AHCI_SSTS_DET_PRES) return false;

    /* Stop port to reconfigure */
    port_stop(p);

    /* Allocate command list (1 KB aligned) */
    void *cl = kzalloc(1024);
    if (!cl) return false;
    uint64_t cl_phys = (uint64_t)(uintptr_t)cl - KERNEL_VIRT_BASE;
    p->clb  = (uint32_t)(cl_phys & 0xFFFFFFFF);
    p->clbu = (uint32_t)(cl_phys >> 32);

    /* Allocate FIS receive buffer (256 bytes) */
    void *fb = kzalloc(256);
    if (!fb) { kfree(cl); return false; }
    uint64_t fb_phys = (uint64_t)(uintptr_t)fb - KERNEL_VIRT_BASE;
    p->fb  = (uint32_t)(fb_phys & 0xFFFFFFFF);
    p->fbu = (uint32_t)(fb_phys >> 32);

    /* Allocate command table (must be 128-byte aligned, 1 PRD entry) */
    void *ct = kzalloc(sizeof(ahci_cmd_table_t) + 128);
    /* Align to 128 bytes */
    ct = (void *)(ALIGN_UP((uint64_t)(uintptr_t)ct, 128));
    uint64_t ct_phys = (uint64_t)(uintptr_t)ct - KERNEL_VIRT_BASE;

    ahci_cmd_hdr_t *hdr = (ahci_cmd_hdr_t *)cl;
    hdr[0].ctba  = (uint32_t)(ct_phys & 0xFFFFFFFF);
    hdr[0].ctbau = (uint32_t)(ct_phys >> 32);

    /* Clear errors */
    p->serr = p->serr;
    p->is   = p->is;

    /* Start port */
    port_start(p);

    ahci_port_t *dp = &ports[port_count];
    dp->port_num  = pnum;
    dp->regs      = p;
    dp->cmd_list  = (ahci_cmd_hdr_t *)cl;
    dp->cmd_table = (ahci_cmd_table_t *)ct;
    dp->fis_buf   = (uint8_t *)fb;
    dp->present   = true;

    return true;
}

/* ---- Issue a command ---- */

static int ahci_issue(ahci_port_t *dp, bool write, uint64_t lba,
                      uint32_t sectors, void *buf)
{
    ahci_port_regs_t *p = dp->regs;
    ahci_cmd_hdr_t   *hdr = dp->cmd_list;
    ahci_cmd_table_t *ct  = dp->cmd_table;

    memset(ct, 0, sizeof(*ct));

    /* Build H2D FIS */
    ahci_fis_h2d_t *fis = (ahci_fis_h2d_t *)ct->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c        = 1;
    fis->command  = write ? ATA_CMD_DMA_WRITE : ATA_CMD_DMA_READ;
    fis->lba0     = (uint8_t)(lba & 0xFF);
    fis->lba1     = (uint8_t)((lba >> 8)  & 0xFF);
    fis->lba2     = (uint8_t)((lba >> 16) & 0xFF);
    fis->device   = (1 << 6);   /* LBA mode */
    fis->lba3     = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4     = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba5     = (uint8_t)((lba >> 40) & 0xFF);
    fis->count    = (uint16_t)sectors;

    /* Set up PRD */
    uint64_t buf_phys = (uint64_t)(uintptr_t)buf - KERNEL_VIRT_BASE;
    ct->prd[0].dba  = (uint32_t)(buf_phys & 0xFFFFFFFF);
    ct->prd[0].dbau = (uint32_t)(buf_phys >> 32);
    ct->prd[0].dbc  = sectors * 512 - 1;
    ct->prd[0].i    = 1;

    /* Command header */
    hdr[0].cfl   = sizeof(ahci_fis_h2d_t) / 4;
    hdr[0].w     = write ? 1 : 0;
    hdr[0].prdtl = 1;
    hdr[0].prdbc = 0;

    /* Issue slot 0 */
    p->is   = 0xFFFFFFFF;
    p->ci   = 1;

    /* Poll for completion */
    uint32_t timeout = 1000000;
    while (timeout--) {
        if (!(p->ci & 1)) break;
        if (p->is & (1 << 30)) return -1;  /* Task file error */
    }
    if (timeout == 0) {
        serial_printf("[AHCI] port %d: command timeout\n", dp->port_num);
        return -1;
    }
    if (p->tfd & (AHCI_TFD_ERR | AHCI_TFD_BSY)) return -1;
    return 0;
}

/* ---- Identify a port's drive ---- */

static void ahci_identify_port(ahci_port_t *dp)
{
    uint16_t *ident = (uint16_t *)kzalloc(512);
    if (!ident) return;

    ahci_port_regs_t *p  = dp->regs;
    ahci_cmd_hdr_t   *hdr = dp->cmd_list;
    ahci_cmd_table_t *ct  = dp->cmd_table;

    memset(ct, 0, sizeof(*ct));

    bool is_satapi = (p->sig == AHCI_SIG_SATAPI);
    dp->is_atapi = is_satapi;

    ahci_fis_h2d_t *fis = (ahci_fis_h2d_t *)ct->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c        = 1;
    fis->command  = is_satapi ? ATA_CMD_IDENTIFY_PACKET : ATA_CMD_IDENTIFY;
    fis->device   = 0;

    uint64_t buf_phys = (uint64_t)(uintptr_t)ident - KERNEL_VIRT_BASE;
    ct->prd[0].dba  = (uint32_t)(buf_phys & 0xFFFFFFFF);
    ct->prd[0].dbau = (uint32_t)(buf_phys >> 32);
    ct->prd[0].dbc  = 511;
    ct->prd[0].i    = 1;

    hdr[0].cfl   = sizeof(ahci_fis_h2d_t) / 4;
    hdr[0].w     = 0;
    hdr[0].prdtl = 1;
    hdr[0].prdbc = 0;

    p->is = 0xFFFFFFFF;
    p->ci = 1;
    uint32_t n = 1000000;
    while ((p->ci & 1) && n--);

    /* Model string */
    for (int i = 0; i < 20; i++) {
        dp->model[i*2]   = (char)(ident[27+i] >> 8);
        dp->model[i*2+1] = (char)(ident[27+i] & 0xFF);
    }
    dp->model[40] = '\0';
    for (int i = 39; i >= 0 && dp->model[i] == ' '; i--) dp->model[i] = '\0';

    /* Sector count (LBA48) */
    if ((ident[83] >> 10) & 1) {
        dp->sector_count = ((uint64_t)ident[103] << 48) |
                           ((uint64_t)ident[102] << 32) |
                           ((uint64_t)ident[101] << 16) |
                            (uint64_t)ident[100];
    } else {
        dp->sector_count = ((uint32_t)ident[61] << 16) | ident[60];
    }

    serial_printf("[AHCI] port %d: '%s' %llu sectors\n",
                  dp->port_num, dp->model, dp->sector_count);
    kfree(ident);
}

/* ---- Block device callbacks ---- */

static int ahci_blk_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buf)
{
    ahci_port_t *dp = (ahci_port_t *)dev->private;
    uint8_t *b = (uint8_t *)buf;
    while (count > 0) {
        uint32_t n = (count > 127) ? 127 : count;
        if (ahci_issue(dp, false, lba, n, b) != 0) return -1;
        lba += n; b += n * 512; count -= n;
    }
    return 0;
}

static int ahci_blk_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buf)
{
    ahci_port_t *dp = (ahci_port_t *)dev->private;
    const uint8_t *b = (const uint8_t *)buf;
    while (count > 0) {
        uint32_t n = (count > 127) ? 127 : count;
        if (ahci_issue(dp, true, lba, n, (void *)b) != 0) return -1;
        lba += n; b += n * 512; count -= n;
    }
    return 0;
}

/* ---- Public: ahci_init ---- */

bool ahci_init(void)
{
    pci_device_t *pdev = pci_find_class(0x01, 0x06);
    if (!pdev) {
        serial_puts("[AHCI] no controller found\n");
        return false;
    }

    serial_printf("[AHCI] found %04x:%04x at %02x:%02x.%u\n",
                  pdev->vendor_id, pdev->device_id,
                  pdev->bus, pdev->device, pdev->function);

    pci_enable_bus_master(pdev);
    pci_enable_memory_space(pdev);

    hba = (ahci_hba_t *)pci_map_bar(pdev, 5);
    if (!hba) { serial_puts("[AHCI] BAR5 map failed\n"); return false; }

    /* Enable AHCI mode */
    hba->ghc |= AHCI_GHC_AE;

    /* Reset HBA */
    hba->ghc |= AHCI_GHC_HR;
    uint32_t t = 1000000;
    while ((hba->ghc & AHCI_GHC_HR) && t--);

    hba->ghc |= AHCI_GHC_AE;

    uint32_t pi = hba->pi;
    port_count = 0;

    for (int pnum = 0; pnum < 32; pnum++) {
        if (!(pi & (1U << pnum))) continue;
        if (!port_init(pnum)) continue;

        ahci_port_t *dp = &ports[port_count - 1];  /* port_init increments through port_count indirectly */
        /* Actually port_init fills ports[port_count] then doesn't increment — fix: */
        dp = &ports[port_count];
        dp->port_num = pnum;
        if (!port_init(pnum)) { continue; }

        ahci_identify_port(dp);

        if (!dp->is_atapi && dp->sector_count > 0) {
            char name[BLKDEV_NAME_LEN];
            snprintf(name, sizeof(name), "sata%d", port_count);
            memcpy(dp->blkdev.name, name, BLKDEV_NAME_LEN);
            dp->blkdev.sector_count = dp->sector_count;
            dp->blkdev.sector_size  = 512;
            dp->blkdev.read_only    = false;
            dp->blkdev.private      = dp;
            dp->blkdev.read         = ahci_blk_read;
            dp->blkdev.write        = ahci_blk_write;
            blkdev_register(&dp->blkdev);
        }
        port_count++;
    }

    serial_printf("[AHCI] init OK — %d port(s) active\n", port_count);
    return port_count > 0;
}
