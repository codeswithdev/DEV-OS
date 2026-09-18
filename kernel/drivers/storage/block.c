#include "block.h"
#include "../../arch/x86_64/serial.h"
#include "../../lib/string.h"
#include "../../lib/printf.h"
#include "../../drivers/vga.h"

static block_device_t *blkdevs[BLKDEV_MAX];
static int             blkdev_cnt = 0;

int blkdev_register(block_device_t *dev)
{
    if (!dev || blkdev_cnt >= BLKDEV_MAX) return -1;
    blkdevs[blkdev_cnt++] = dev;
    serial_printf("[BLKDEV] registered '%s': %llu sectors x %u bytes\n",
                  dev->name, dev->sector_count, dev->sector_size);
    return blkdev_cnt - 1;
}

block_device_t *blkdev_get(int index)
{
    if (index < 0 || index >= blkdev_cnt) return NULL;
    return blkdevs[index];
}

int blkdev_count(void)
{
    return blkdev_cnt;
}

void blkdev_print_all(void)
{
    if (blkdev_cnt == 0) {
        vga_puts("No block devices registered\n");
        serial_puts("No block devices registered\n");
        return;
    }
    char buf[80];
    for (int i = 0; i < blkdev_cnt; i++) {
        block_device_t *d = blkdevs[i];
        uint64_t size_mb = (d->sector_count * d->sector_size) >> 20;
        snprintf(buf, sizeof(buf),
                 "  %s: %llu sectors x %u bytes = %llu MB%s\n",
                 d->name, d->sector_count, d->sector_size, size_mb,
                 d->read_only ? " [ro]" : "");
        vga_puts(buf);
        serial_puts(buf);
    }
}
