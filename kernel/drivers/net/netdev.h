/* DevOS — Network device abstraction (netdev.h) */
#pragma once
#include "../../include/types.h"

#define NETDEV_MAX      8
#define NETDEV_NAME_LEN 16
#define ETH_ALEN        6

typedef struct net_device net_device_t;

struct net_device {
    char    name[NETDEV_NAME_LEN];
    uint8_t mac[ETH_ALEN];
    bool    up;
    void   *private;

    int (*send)   (net_device_t *dev, const void *data, size_t len);
    void (*poll)  (net_device_t *dev);  /* check for received packets */
};

typedef void (*netdev_rx_handler_t)(net_device_t *dev, const void *data, size_t len);

int          netdev_register(net_device_t *dev);
net_device_t *netdev_get(int index);
int          netdev_count(void);
void         netdev_set_rx_handler(netdev_rx_handler_t fn);
void         netdev_dispatch_rx(net_device_t *dev, const void *data, size_t len);
void         netdev_print_all(void);   /* shell 'netinfo' */
