/*
 * DevOS — Block device abstraction (block.h)
 * Provides a uniform interface over ATA PIO, AHCI, NVMe, and USB MSC.
 */
#pragma once
#include "../../include/types.h"

#define BLKDEV_MAX  16
#define BLKDEV_NAME_LEN 16

typedef struct block_device {
    char     name[BLKDEV_NAME_LEN];
    uint64_t sector_count;
    uint32_t sector_size;
    bool     read_only;
    void    *private;   /* driver-specific context */

    int (*read) (struct block_device *dev, uint64_t lba,
                 uint32_t count, void *buf);
    int (*write)(struct block_device *dev, uint64_t lba,
                 uint32_t count, const void *buf);
} block_device_t;

int            blkdev_register(block_device_t *dev);
block_device_t *blkdev_get(int index);
int            blkdev_count(void);
void           blkdev_print_all(void);   /* shell 'diskinfo' */
