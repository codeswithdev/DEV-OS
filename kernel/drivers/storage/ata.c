/*
 * DevOS — ATA PIO driver
 *
 * Supports primary (0x1F0) and secondary (0x170) ATA channels in polling
 * (PIO) mode.  No DMA.  Works in QEMU with -drive format=raw,...
 *
 * Channel layout:
 *   Drive 0 = Primary Master   (channel 0, select 0xA0)
 *   Drive 1 = Primary Slave    (channel 0, select 0xB0)
 *   Drive 2 = Secondary Master (channel 1, select 0xA0)
 *   Drive 3 = Secondary Slave  (channel 1, select 0xB0)
 *
 * Standard ATA registers (all relative to channel base):
 *   +0  Data           +1  Error/Features  +2  Sector Count
 *   +3  LBA lo (7:0)   +4  LBA mid (15:8)  +5  LBA hi (23:16)
 *   +6  Drive/Head     +7  Status/Command
 *   +0x206 (base+0x200+6) = Device Control / Alt Status
 */
#include "ata.h"
#include "block.h"
#include "../../arch/x86_64/serial.h"
#include "../../mm/heap.h"
#include "../../lib/string.h"

/* ATA channel I/O bases */
static const uint16_t ata_base[2] = { 0x1F0, 0x170 };
static const uint16_t ata_ctrl[2] = { 0x3F6, 0x376 };

/* ATA register offsets */
#define ATA_DATA        0
#define ATA_ERROR       1
#define ATA_FEATURES    1
#define ATA_SECT_CNT    2
#define ATA_LBA_LO      3
#define ATA_LBA_MID     4
#define ATA_LBA_HI      5
#define ATA_DRIVE_SEL   6
#define ATA_STATUS      7
#define ATA_CMD         7

/* ATA status bits */
#define ATA_SR_BSY      0x80
#define ATA_SR_DRDY     0x40
#define ATA_SR_DF       0x20
#define ATA_SR_DSC      0x10
#define ATA_SR_DRQ      0x08
#define ATA_SR_CORR     0x04
#define ATA_SR_IDX      0x02
#define ATA_SR_ERR      0x01

/* ATA commands */
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_READ28      0x20
#define ATA_CMD_WRITE28     0x30
#define ATA_CMD_CACHE_FLUSH 0xE7

ata_drive_t ata_drives[ATA_MAX_DRIVES];

/* ---- I/O helpers ---- */
static inline uint8_t inb(uint16_t p)
{
    uint8_t v; __asm__ __volatile__("inb %1,%0":"=a"(v):"Nd"(p)); return v;
}
static inline uint16_t inw(uint16_t p)
{
    uint16_t v; __asm__ __volatile__("inw %1,%0":"=a"(v):"Nd"(p)); return v;
}
static inline void outb(uint16_t p, uint8_t v)
{
    __asm__ __volatile__("outb %0,%1"::"a"(v),"Nd"(p):"memory");
}

/* 400-ns delay: read alt-status 4 times */
static inline void ata_delay(uint16_t ctrl)
{
    inb(ctrl); inb(ctrl); inb(ctrl); inb(ctrl);
}

/* Poll BSY flag; returns 0 on timeout */
static int ata_wait_not_busy(uint16_t base, uint32_t timeout_ms)
{
    uint32_t n = timeout_ms * 10000;
    while (n--) {
        if (!(inb((uint16_t)(base + ATA_STATUS)) & ATA_SR_BSY)) return 1;
    }
    return 0;
}

/* Wait for DRQ (data request ready) */
static int ata_wait_drq(uint16_t base, uint32_t timeout_ms)
{
    uint32_t n = timeout_ms * 10000;
    while (n--) {
        uint8_t st = inb((uint16_t)(base + ATA_STATUS));
        if (st & ATA_SR_ERR) return -1;
        if (st & ATA_SR_DRQ) return 1;
    }
    return 0;
}

/* ---- IDENTIFY DEVICE ---- */
static void ata_identify(int chan, int sel, int drive_idx)
{
    uint16_t base = ata_base[chan];
    uint16_t ctrl = ata_ctrl[chan];
    ata_drive_t *d = &ata_drives[drive_idx];
    d->present = false;

    /* Select drive */
    outb((uint16_t)(base + ATA_DRIVE_SEL), (uint8_t)(sel ? 0xB0 : 0xA0));
    ata_delay(ctrl);

    /* Issue IDENTIFY */
    outb((uint16_t)(base + ATA_CMD), ATA_CMD_IDENTIFY);
    ata_delay(ctrl);

    /* Check if drive exists */
    uint8_t st = inb((uint16_t)(base + ATA_STATUS));
    if (st == 0) return;    /* no drive */

    /* Wait for BSY to clear */
    if (!ata_wait_not_busy(base, 5000)) {
        serial_printf("[ATA] drive %d: IDENTIFY timeout\n", drive_idx);
        return;
    }

    st = inb((uint16_t)(base + ATA_STATUS));
    if (st & ATA_SR_ERR) {
        /* Might be ATAPI — check signature */
        uint8_t mid = inb((uint16_t)(base + ATA_LBA_MID));
        uint8_t hi  = inb((uint16_t)(base + ATA_LBA_HI));
        if (mid == 0x14 && hi == 0xEB) {
            d->present  = true;
            d->is_atapi = true;
            memcpy(d->model, "ATAPI device", 13);
            serial_printf("[ATA] drive %d: ATAPI\n", drive_idx);
        }
        return;
    }

    if (!ata_wait_drq(base, 5000)) {
        serial_printf("[ATA] drive %d: DRQ timeout\n", drive_idx);
        return;
    }

    /* Read 256 words of IDENTIFY data */
    uint16_t ident[256];
    for (int i = 0; i < 256; i++) ident[i] = inw(base);

    /* Extract model string (words 27–46, big-endian byte swap) */
    for (int i = 0; i < 20; i++) {
        d->model[i*2]   = (char)(ident[27+i] >> 8);
        d->model[i*2+1] = (char)(ident[27+i] & 0xFF);
    }
    d->model[40] = '\0';
    /* Trim trailing spaces */
    for (int i = 39; i >= 0 && d->model[i] == ' '; i--) d->model[i] = '\0';

    /* Sector counts */
    d->lba28_sectors = ((uint32_t)ident[61] << 16) | ident[60];
    d->lba48 = (ident[83] >> 10) & 1;
    if (d->lba48) {
        d->lba48_sectors = ((uint64_t)ident[103] << 48) |
                           ((uint64_t)ident[102] << 32) |
                           ((uint64_t)ident[101] << 16) |
                            (uint64_t)ident[100];
    }

    d->present = true;
    serial_printf("[ATA] drive %d: '%s' %u LBA28 sectors%s\n",
                  drive_idx, d->model, d->lba28_sectors,
                  d->lba48 ? " (LBA48)" : "");
}

/* ---- PIO Read / Write helpers ---- */

static int ata_do_read(int chan, int sel, uint32_t lba, uint8_t count, void *buf)
{
    uint16_t base = ata_base[chan];
    uint16_t ctrl = ata_ctrl[chan];

    if (!ata_wait_not_busy(base, 5000)) return -1;

    outb((uint16_t)(base + ATA_DRIVE_SEL),
         (uint8_t)(0xE0 | (sel << 4) | ((lba >> 24) & 0x0F)));
    outb((uint16_t)(base + ATA_SECT_CNT), count);
    outb((uint16_t)(base + ATA_LBA_LO),  (uint8_t)(lba & 0xFF));
    outb((uint16_t)(base + ATA_LBA_MID), (uint8_t)((lba >> 8)  & 0xFF));
    outb((uint16_t)(base + ATA_LBA_HI),  (uint8_t)((lba >> 16) & 0xFF));
    outb((uint16_t)(base + ATA_CMD),     ATA_CMD_READ28);
    ata_delay(ctrl);

    uint16_t *ptr = (uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (!ata_wait_not_busy(base, 5000)) return -1;
        int r = ata_wait_drq(base, 5000);
        if (r < 0) return -1;
        if (r == 0) return -1;
        for (int i = 0; i < 256; i++) ptr[s * 256 + i] = inw(base);
    }
    return 0;
}

static int ata_do_write(int chan, int sel, uint32_t lba, uint8_t count, const void *buf)
{
    uint16_t base = ata_base[chan];
    uint16_t ctrl = ata_ctrl[chan];

    if (!ata_wait_not_busy(base, 5000)) return -1;

    outb((uint16_t)(base + ATA_DRIVE_SEL),
         (uint8_t)(0xE0 | (sel << 4) | ((lba >> 24) & 0x0F)));
    outb((uint16_t)(base + ATA_SECT_CNT), count);
    outb((uint16_t)(base + ATA_LBA_LO),  (uint8_t)(lba & 0xFF));
    outb((uint16_t)(base + ATA_LBA_MID), (uint8_t)((lba >> 8)  & 0xFF));
    outb((uint16_t)(base + ATA_LBA_HI),  (uint8_t)((lba >> 16) & 0xFF));
    outb((uint16_t)(base + ATA_CMD),     ATA_CMD_WRITE28);
    ata_delay(ctrl);

    const uint16_t *ptr = (const uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (!ata_wait_not_busy(base, 5000)) return -1;
        int r = ata_wait_drq(base, 5000);
        if (r <= 0) return -1;
        for (int i = 0; i < 256; i++) {
            __asm__ __volatile__("outw %0,%1"
                :: "a"(ptr[s*256+i]), "Nd"(base) : "memory");
        }
    }
    outb((uint16_t)(base + ATA_CMD), ATA_CMD_CACHE_FLUSH);
    ata_wait_not_busy(base, 5000);
    return 0;
}

/* ---- Block device callbacks ---- */

static int ata_blk_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buf)
{
    int drive = (int)(uint64_t)dev->private;
    int chan  = drive / 2;
    int sel   = drive % 2;
    /* Read up to 255 sectors at a time */
    uint8_t *b = (uint8_t *)buf;
    while (count > 0) {
        uint8_t n = (count > 255) ? 255 : (uint8_t)count;
        if (ata_do_read(chan, sel, (uint32_t)lba, n, b) != 0) return -1;
        lba += n; b += n * 512; count -= n;
    }
    return 0;
}

static int ata_blk_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buf)
{
    int drive = (int)(uint64_t)dev->private;
    int chan  = drive / 2;
    int sel   = drive % 2;
    const uint8_t *b = (const uint8_t *)buf;
    while (count > 0) {
        uint8_t n = (count > 255) ? 255 : (uint8_t)count;
        if (ata_do_write(chan, sel, (uint32_t)lba, n, b) != 0) return -1;
        lba += n; b += n * 512; count -= n;
    }
    return 0;
}

/* ---- Static block device objects for ATA drives ---- */
static block_device_t ata_blkdevs[ATA_MAX_DRIVES];
static char ata_blkdev_names[ATA_MAX_DRIVES][BLKDEV_NAME_LEN] = {
    "ata0", "ata1", "ata2", "ata3"
};

/* ---- Public: ata_read_sectors / ata_write_sectors ---- */
int ata_read_sectors(int drive, uint32_t lba, uint8_t count, void *buf)
{
    if (drive < 0 || drive >= ATA_MAX_DRIVES || !ata_drives[drive].present) return -1;
    return ata_do_read(drive / 2, drive % 2, lba, count, buf);
}

int ata_write_sectors(int drive, uint32_t lba, uint8_t count, const void *buf)
{
    if (drive < 0 || drive >= ATA_MAX_DRIVES || !ata_drives[drive].present) return -1;
    return ata_do_write(drive / 2, drive % 2, lba, count, buf);
}

void ata_init(void)
{
    serial_puts("[ATA] scanning channels...\n");
    memset(ata_drives, 0, sizeof(ata_drives));

    for (int chan = 0; chan < 2; chan++) {
        for (int sel = 0; sel < 2; sel++) {
            int idx = chan * 2 + sel;
            ata_identify(chan, sel, idx);

            if (ata_drives[idx].present && !ata_drives[idx].is_atapi) {
                block_device_t *bd = &ata_blkdevs[idx];
                memcpy(bd->name, ata_blkdev_names[idx], BLKDEV_NAME_LEN);
                bd->sector_count = ata_drives[idx].lba48
                    ? ata_drives[idx].lba48_sectors
                    : ata_drives[idx].lba28_sectors;
                bd->sector_size  = 512;
                bd->read_only    = false;
                bd->private      = (void *)(uint64_t)idx;
                bd->read         = ata_blk_read;
                bd->write        = ata_blk_write;
                blkdev_register(bd);
            }
        }
    }
    serial_puts("[ATA] init complete\n");
}
