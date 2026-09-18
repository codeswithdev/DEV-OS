/*
 * DevOS — NVMe driver
 *
 * Implements NVM Express (NVMe) 1.x support via PCI class 0x01/0x08.
 * Supports a single namespace (NSID=1) which covers modern QEMU NVMe.
 *
 * NVMe controller MMIO offsets (all from BAR0):
 *   0x00  CAP   Controller Capabilities
 *   0x08  VS    Version
 *   0x0C  INTMS Interrupt Mask Set
 *   0x10  INTMC Interrupt Mask Clear
 *   0x14  CC    Controller Configuration
 *   0x1C  CSTS  Controller Status
 *   0x24  AQA   Admin Queue Attributes
 *   0x28  ASQ   Admin Submission Queue Base
 *   0x30  ACQ   Admin Completion Queue Base
 *   0x1000 + n*8  Submission Queue n Doorbell
 *   0x1000 + n*8 + 4  Completion Queue n Doorbell
 *
 * Admin commands used here:
 *   0x06  Identify
 *   0x05  Create I/O Completion Queue
 *   0x01  Create I/O Submission Queue
 *
 * I/O commands:
 *   0x02  Read
 *   0x01  Write
 */
#include "nvme.h"
#include "block.h"
#include "../pci/pci.h"
#include "../../mm/heap.h"
#include "../../mm/vmm.h"
#include "../../arch/x86_64/serial.h"
#include "../../lib/string.h"

/* ---- NVMe register offsets ---- */
#define NVME_CAP    0x00
#define NVME_VS     0x08
#define NVME_INTMS  0x0C
#define NVME_INTMC  0x10
#define NVME_CC     0x14
#define NVME_CSTS   0x1C
#define NVME_AQA    0x24
#define NVME_ASQ    0x28
#define NVME_ACQ    0x30

/* CC register bits */
#define NVME_CC_EN          (1 << 0)
#define NVME_CC_CSS_NVM     (0 << 4)
#define NVME_CC_MPS_4K      (0 << 7)   /* Memory Page Size = 4096 (2^(12+0)) */
#define NVME_CC_AMS_RR      (0 << 11)
#define NVME_CC_IOSQES(n)   ((n) << 16)
#define NVME_CC_IOCQES(n)   ((n) << 20)

/* CSTS register bits */
#define NVME_CSTS_RDY       (1 << 0)
#define NVME_CSTS_CFS       (1 << 1)

#define NVME_QUEUE_DEPTH    64
#define NVME_SQ_ENTRY_SIZE  64   /* 64 bytes per submission queue entry */
#define NVME_CQ_ENTRY_SIZE  16   /* 16 bytes per completion queue entry */

typedef volatile struct {
    uint32_t dw[16];   /* 64-byte command: 16 DWORDs */
} PACKED nvme_sq_entry_t;

typedef volatile struct {
    uint32_t cmd_specific;
    uint32_t reserved;
    uint16_t sqhd;     /* SQ Head Pointer */
    uint16_t sqid;     /* SQ Identifier */
    uint16_t cid;      /* Command Identifier */
    uint16_t status;   /* P bit (bit0) and status field */
} PACKED nvme_cq_entry_t;

typedef struct {
    volatile uint8_t   *bar;
    nvme_sq_entry_t    *asq;       /* Admin Submission Queue */
    nvme_cq_entry_t    *acq;       /* Admin Completion Queue */
    nvme_sq_entry_t    *iosq;      /* I/O Submission Queue */
    nvme_cq_entry_t    *iocq;      /* I/O Completion Queue */
    uint16_t            asq_tail;
    uint16_t            acq_head;
    uint8_t             acq_phase; /* Expected Phase Bit */
    uint16_t            iosq_tail;
    uint16_t            iocq_head;
    uint8_t             iocq_phase;
    uint16_t            next_cid;
    uint32_t            db_stride; /* Doorbell stride (bytes) */
    uint64_t            ns_size;   /* Namespace 1 LBA count */
    uint32_t            lba_size;  /* LBA data size in bytes */
    block_device_t      blkdev;
} nvme_ctrl_t;

static nvme_ctrl_t ctrl;

/* ---- MMIO helpers ---- */
static inline uint32_t nvme_read32(uint32_t off)
{
    return *(volatile uint32_t *)(ctrl.bar + off);
}
static inline uint64_t nvme_read64(uint32_t off)
{
    return *(volatile uint64_t *)(ctrl.bar + off);
}
static inline void nvme_write32(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(ctrl.bar + off) = val;
}
static inline void nvme_write64(uint32_t off, uint64_t val)
{
    *(volatile uint64_t *)(ctrl.bar + off) = val;
}

/* Doorbell register address for queue qid (0=admin, 1=IO) */
static inline volatile uint32_t *nvme_sq_db(uint16_t qid)
{
    return (volatile uint32_t *)(ctrl.bar + 0x1000 + qid * ctrl.db_stride * 2);
}
static inline volatile uint32_t *nvme_cq_db(uint16_t qid)
{
    return (volatile uint32_t *)(ctrl.bar + 0x1000 + qid * ctrl.db_stride * 2 + ctrl.db_stride);
}

/* ---- Admin command submit + wait ---- */

static uint16_t nvme_alloc_cid(void)
{
    return ctrl.next_cid++;
}

static int nvme_submit_admin(nvme_sq_entry_t *cmd)
{
    ctrl.asq[ctrl.asq_tail] = *cmd;
    ctrl.asq_tail = (ctrl.asq_tail + 1) % NVME_QUEUE_DEPTH;
    *nvme_sq_db(0) = ctrl.asq_tail;

    /* Wait for completion */
    uint32_t timeout = 2000000;
    while (timeout--) {
        nvme_cq_entry_t *cqe = &ctrl.acq[ctrl.acq_head];
        if ((cqe->status & 1) == ctrl.acq_phase) {
            /* Advance head */
            ctrl.acq_head = (ctrl.acq_head + 1) % NVME_QUEUE_DEPTH;
            if (ctrl.acq_head == 0) ctrl.acq_phase ^= 1;
            *nvme_cq_db(0) = ctrl.acq_head;
            uint16_t sc = (cqe->status >> 1) & 0xFF;
            if (sc != 0) {
                serial_printf("[NVMe] admin cmd error status=0x%x\n", cqe->status);
                return -1;
            }
            return 0;
        }
    }
    serial_puts("[NVMe] admin command timeout\n");
    return -1;
}

/* ---- I/O command submit + wait ---- */

static int nvme_submit_io(nvme_sq_entry_t *cmd)
{
    ctrl.iosq[ctrl.iosq_tail] = *cmd;
    ctrl.iosq_tail = (ctrl.iosq_tail + 1) % NVME_QUEUE_DEPTH;
    *nvme_sq_db(1) = ctrl.iosq_tail;

    uint32_t timeout = 2000000;
    while (timeout--) {
        nvme_cq_entry_t *cqe = &ctrl.iocq[ctrl.iocq_head];
        if ((cqe->status & 1) == ctrl.iocq_phase) {
            ctrl.iocq_head = (ctrl.iocq_head + 1) % NVME_QUEUE_DEPTH;
            if (ctrl.iocq_head == 0) ctrl.iocq_phase ^= 1;
            *nvme_cq_db(1) = ctrl.iocq_head;
            uint16_t sc = (cqe->status >> 1) & 0xFF;
            if (sc != 0) return -1;
            return 0;
        }
    }
    serial_puts("[NVMe] I/O command timeout\n");
    return -1;
}

/* ---- Identify controller ---- */

static int nvme_identify_ctrl(void)
{
    uint8_t *buf = (uint8_t *)kzalloc(4096);
    if (!buf) return -1;
    uint64_t phys = (uint64_t)(uintptr_t)buf - KERNEL_VIRT_BASE;

    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.dw[0] = 0x06 | (nvme_alloc_cid() << 16);  /* Identify, cid */
    cmd.dw[6] = (uint32_t)(phys & 0xFFFFFFFF);
    cmd.dw[7] = (uint32_t)(phys >> 32);
    cmd.dw[10] = 1;   /* CNS=1: Identify Controller */

    int r = nvme_submit_admin(&cmd);
    if (r == 0) {
        char mn[41];
        memcpy(mn, buf + 24, 40);
        mn[40] = '\0';
        for (int i = 39; i >= 0 && mn[i] == ' '; i--) mn[i] = '\0';
        serial_printf("[NVMe] controller: '%s'\n", mn);
    }
    kfree(buf);
    return r;
}

/* ---- Identify namespace 1 ---- */

static int nvme_identify_ns(void)
{
    uint8_t *buf = (uint8_t *)kzalloc(4096);
    if (!buf) return -1;
    uint64_t phys = (uint64_t)(uintptr_t)buf - KERNEL_VIRT_BASE;

    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.dw[0] = 0x06 | (nvme_alloc_cid() << 16);
    cmd.dw[1] = 1;    /* NSID = 1 */
    cmd.dw[6] = (uint32_t)(phys & 0xFFFFFFFF);
    cmd.dw[7] = (uint32_t)(phys >> 32);
    cmd.dw[10] = 0;   /* CNS=0: Identify Namespace */

    int r = nvme_submit_admin(&cmd);
    if (r == 0) {
        ctrl.ns_size = *(uint64_t *)buf;           /* NSZE */
        uint8_t lbaf_idx = buf[26] & 0x0F;        /* Current LBA format index */
        uint32_t lbaf = *(uint32_t *)(buf + 128 + lbaf_idx * 4);
        ctrl.lba_size = 1U << ((lbaf >> 16) & 0xFF);
        if (ctrl.lba_size == 0) ctrl.lba_size = 512;
        serial_printf("[NVMe] NS1: %llu LBAs x %u bytes\n",
                      ctrl.ns_size, ctrl.lba_size);
    }
    kfree(buf);
    return r;
}

/* ---- Block device callbacks ---- */

static int nvme_blk_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buf)
{
    (void)dev;
    uint8_t *b = (uint8_t *)buf;
    while (count > 0) {
        uint32_t n = (count > 32) ? 32 : count;
        uint64_t phys = (uint64_t)(uintptr_t)b - KERNEL_VIRT_BASE;
        nvme_sq_entry_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.dw[0]  = 0x02 | (nvme_alloc_cid() << 16);  /* Read */
        cmd.dw[1]  = 1;    /* NSID */
        cmd.dw[6]  = (uint32_t)(phys & 0xFFFFFFFF);
        cmd.dw[7]  = (uint32_t)(phys >> 32);
        cmd.dw[10] = (uint32_t)(lba & 0xFFFFFFFF);
        cmd.dw[11] = (uint32_t)(lba >> 32);
        cmd.dw[12] = n - 1;   /* NLB (0-based) */
        if (nvme_submit_io(&cmd) != 0) return -1;
        lba += n; b += n * ctrl.lba_size; count -= n;
    }
    return 0;
}

static int nvme_blk_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buf)
{
    (void)dev;
    const uint8_t *b = (const uint8_t *)buf;
    while (count > 0) {
        uint32_t n = (count > 32) ? 32 : count;
        uint64_t phys = (uint64_t)(uintptr_t)b - KERNEL_VIRT_BASE;
        nvme_sq_entry_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.dw[0]  = 0x01 | (nvme_alloc_cid() << 16);  /* Write */
        cmd.dw[1]  = 1;
        cmd.dw[6]  = (uint32_t)(phys & 0xFFFFFFFF);
        cmd.dw[7]  = (uint32_t)(phys >> 32);
        cmd.dw[10] = (uint32_t)(lba & 0xFFFFFFFF);
        cmd.dw[11] = (uint32_t)(lba >> 32);
        cmd.dw[12] = n - 1;
        if (nvme_submit_io(&cmd) != 0) return -1;
        lba += n; b += n * ctrl.lba_size; count -= n;
    }
    return 0;
}

/* ---- Public: nvme_init ---- */

bool nvme_init(void)
{
    pci_device_t *pdev = pci_find_class(0x01, 0x08);
    if (!pdev) {
        serial_puts("[NVMe] no controller found\n");
        return false;
    }

    serial_printf("[NVMe] found %04x:%04x\n", pdev->vendor_id, pdev->device_id);
    pci_enable_bus_master(pdev);
    pci_enable_memory_space(pdev);

    ctrl.bar = (volatile uint8_t *)pci_map_bar(pdev, 0);
    if (!ctrl.bar) { serial_puts("[NVMe] BAR0 map failed\n"); return false; }

    /* Read capabilities */
    uint64_t cap = nvme_read64(NVME_CAP);
    uint8_t  mqes = (uint8_t)(cap & 0xFFFF);            /* Max Queue Entries - 1 */
    ctrl.db_stride = (uint32_t)(1 << (((cap >> 32) & 0xF) + 2));

    serial_printf("[NVMe] CAP: mqes=%u db_stride=%u\n", mqes + 1, ctrl.db_stride);

    /* Disable controller */
    nvme_write32(NVME_CC, 0);
    uint32_t t = 2000000;
    while ((nvme_read32(NVME_CSTS) & NVME_CSTS_RDY) && t--);
    if (!t) { serial_puts("[NVMe] disable timeout\n"); return false; }

    /* Allocate admin queues */
    ctrl.asq = (nvme_sq_entry_t *)kzalloc(NVME_QUEUE_DEPTH * NVME_SQ_ENTRY_SIZE);
    ctrl.acq = (nvme_cq_entry_t *)kzalloc(NVME_QUEUE_DEPTH * NVME_CQ_ENTRY_SIZE);
    if (!ctrl.asq || !ctrl.acq) return false;

    uint64_t asq_phys = (uint64_t)(uintptr_t)ctrl.asq - KERNEL_VIRT_BASE;
    uint64_t acq_phys = (uint64_t)(uintptr_t)ctrl.acq - KERNEL_VIRT_BASE;

    nvme_write32(NVME_AQA, ((NVME_QUEUE_DEPTH - 1) << 16) | (NVME_QUEUE_DEPTH - 1));
    nvme_write64(NVME_ASQ, asq_phys);
    nvme_write64(NVME_ACQ, acq_phys);

    /* Enable controller */
    uint32_t cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS_4K |
                  NVME_CC_AMS_RR | NVME_CC_IOSQES(6) | NVME_CC_IOCQES(4);
    nvme_write32(NVME_CC, cc);

    t = 2000000;
    while (!(nvme_read32(NVME_CSTS) & NVME_CSTS_RDY) && t--);
    if (!t) { serial_puts("[NVMe] enable timeout\n"); return false; }

    ctrl.asq_tail   = ctrl.acq_head  = 0;
    ctrl.acq_phase  = 1;
    ctrl.next_cid   = 1;

    if (nvme_identify_ctrl() != 0) return false;

    /* Create I/O Completion Queue (QIOCQ) */
    ctrl.iocq = (nvme_cq_entry_t *)kzalloc(NVME_QUEUE_DEPTH * NVME_CQ_ENTRY_SIZE);
    uint64_t iocq_phys = (uint64_t)(uintptr_t)ctrl.iocq - KERNEL_VIRT_BASE;
    {
        nvme_sq_entry_t cmd; memset(&cmd, 0, sizeof(cmd));
        cmd.dw[0]  = 0x05 | (nvme_alloc_cid() << 16);
        cmd.dw[6]  = (uint32_t)(iocq_phys & 0xFFFFFFFF);
        cmd.dw[7]  = (uint32_t)(iocq_phys >> 32);
        cmd.dw[10] = (uint32_t)(((NVME_QUEUE_DEPTH - 1) << 16) | 1);  /* QSIZE, QID=1 */
        cmd.dw[11] = (1 << 0);  /* Physically Contiguous */
        if (nvme_submit_admin(&cmd) != 0) return false;
    }

    /* Create I/O Submission Queue (QIOSQ) */
    ctrl.iosq = (nvme_sq_entry_t *)kzalloc(NVME_QUEUE_DEPTH * NVME_SQ_ENTRY_SIZE);
    uint64_t iosq_phys = (uint64_t)(uintptr_t)ctrl.iosq - KERNEL_VIRT_BASE;
    {
        nvme_sq_entry_t cmd; memset(&cmd, 0, sizeof(cmd));
        cmd.dw[0]  = 0x01 | (nvme_alloc_cid() << 16);
        cmd.dw[6]  = (uint32_t)(iosq_phys & 0xFFFFFFFF);
        cmd.dw[7]  = (uint32_t)(iosq_phys >> 32);
        cmd.dw[10] = (uint32_t)(((NVME_QUEUE_DEPTH - 1) << 16) | 1);
        cmd.dw[11] = (1 << 16) | (1 << 0);  /* CQID=1, Physically Contiguous */
        if (nvme_submit_admin(&cmd) != 0) return false;
    }

    ctrl.iosq_tail = ctrl.iocq_head = 0;
    ctrl.iocq_phase = 1;

    if (nvme_identify_ns() != 0) return false;

    /* Register block device */
    memcpy(ctrl.blkdev.name, "nvme0n1", 8);
    ctrl.blkdev.sector_count = ctrl.ns_size;
    ctrl.blkdev.sector_size  = ctrl.lba_size;
    ctrl.blkdev.read_only    = false;
    ctrl.blkdev.private      = &ctrl;
    ctrl.blkdev.read         = nvme_blk_read;
    ctrl.blkdev.write        = nvme_blk_write;
    blkdev_register(&ctrl.blkdev);

    serial_puts("[NVMe] init OK\n");
    return true;
}
