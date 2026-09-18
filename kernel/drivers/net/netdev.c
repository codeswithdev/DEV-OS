#include "netdev.h"
#include "../../arch/x86_64/serial.h"
#include "../../lib/string.h"
#include "../../lib/printf.h"
#include "../../drivers/vga.h"

static net_device_t    *devs[NETDEV_MAX];
static int              dev_cnt = 0;
static netdev_rx_handler_t rx_handler = NULL;

int netdev_register(net_device_t *dev)
{
    if (!dev || dev_cnt >= NETDEV_MAX) return -1;
    devs[dev_cnt++] = dev;
    serial_printf("[NETDEV] registered '%s' MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
                  dev->name,
                  dev->mac[0], dev->mac[1], dev->mac[2],
                  dev->mac[3], dev->mac[4], dev->mac[5]);
    return dev_cnt - 1;
}

net_device_t *netdev_get(int index)
{
    if (index < 0 || index >= dev_cnt) return NULL;
    return devs[index];
}

int netdev_count(void) { return dev_cnt; }

void netdev_set_rx_handler(netdev_rx_handler_t fn) { rx_handler = fn; }

void netdev_dispatch_rx(net_device_t *dev, const void *data, size_t len)
{
    if (rx_handler) rx_handler(dev, data, len);
}

void netdev_print_all(void)
{
    if (dev_cnt == 0) {
        vga_puts("No network devices\n");
        serial_puts("No network devices\n");
        return;
    }
    char buf[80];
    for (int i = 0; i < dev_cnt; i++) {
        net_device_t *d = devs[i];
        snprintf(buf, sizeof(buf),
                 "  %s  MAC %02x:%02x:%02x:%02x:%02x:%02x  %s\n",
                 d->name,
                 d->mac[0], d->mac[1], d->mac[2],
                 d->mac[3], d->mac[4], d->mac[5],
                 d->up ? "UP" : "DOWN");
        vga_puts(buf);
        serial_puts(buf);
    }
}
