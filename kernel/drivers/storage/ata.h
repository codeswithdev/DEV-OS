/*
 * DevOS — ATA PIO driver (ata.h)
 * LBA28 polling-mode IDE/ATA driver
 */
#pragma once
#include "../../include/types.h"

#define ATA_MAX_DRIVES  4   /* 2 channels × 2 drives */

typedef struct {
    bool     present;
    bool     is_atapi;
    bool     lba48;         /* LBA48 support */
    char     model[41];
    uint32_t lba28_sectors; /* usable for LBA28 (≤ 2^28) */
    uint64_t lba48_sectors;
} ata_drive_t;

extern ata_drive_t ata_drives[ATA_MAX_DRIVES];

void ata_init(void);
int  ata_read_sectors (int drive, uint32_t lba, uint8_t count, void *buf);
int  ata_write_sectors(int drive, uint32_t lba, uint8_t count, const void *buf);
